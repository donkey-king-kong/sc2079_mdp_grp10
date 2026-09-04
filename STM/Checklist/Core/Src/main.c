/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "cmsis_os.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "oled.h"
#include <stdio.h>
#include <string.h>
#include "ICM20948.h"
#include <stdlib.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
/* ==========================================
 *        CALIBRATION CONTROL PANEL
 * ========================================== */

// --- 1. DISTANCE & GYRO CALIBRATION ---
#define TICKS_PER_CM         68.0   // Tweak if "S10" travels more or less than 10cm
#define SLIDE_TICKS_PER_CM   75.19  // Specific tuning multiplier used in slide maneuvers

// --- 2. MOTOR BIAS (HARDWARE OFFSETS) ---
#define RIGHT_MOTOR_BIAS     1.072  // Multiplier for right motor to match left motor speed (Fixes straight-line drift)
#define TURN_SLAVE_RATIO     0.59   // Inner wheel speed multiplier during arc turns

// --- 3. SERVO CALIBRATION ---
#define SERVOCENTER          150
#define SERVOLEFT            100
#define SERVORIGHT           200
#define TURNLEFT_TH          115
#define TURNRIGHT_TH         195

// --- 4. PID POWER STEPS (Max Power = 7199) ---
// Straight Driving Speeds
#define PID_STR_MAX          5500   // ~76% speed (Leave headroom so PID has room to adjust!)
#define PID_STR_HIGH         4500   // ~62% speed
#define PID_STR_MED          3000   // ~41% speed
#define PID_STR_LOW          2000   // ~27% speed
#define PID_STR_MIN          1500   // Minimum power to break static floor friction

// Turning Speeds
#define PID_ANG_MAX          5000   // ~70% speed
#define PID_ANG_HIGH         4000   // ~55% speed
#define PID_ANG_MED          3000   // ~41% speed
#define PID_ANG_LOW          2200   // ~30% speed
#define PID_ANG_FINE         1600   // ~22% speed
#define PID_ANG_MIN          1300   // Minimum power for turning frictions
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
 I2C_HandleTypeDef hi2c2;

TIM_HandleTypeDef htim2;
TIM_HandleTypeDef htim3;
TIM_HandleTypeDef htim4;
TIM_HandleTypeDef htim5;
TIM_HandleTypeDef htim6;
TIM_HandleTypeDef htim9;
TIM_HandleTypeDef htim12;

UART_HandleTypeDef huart3;

/* Definitions for defaultTask */
osThreadId_t defaultTaskHandle;
const osThreadAttr_t defaultTask_attributes = {
  .name = "defaultTask",
  .stack_size = 256 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};
/* Definitions for communicateTask */
osThreadId_t communicateTaskHandle;
const osThreadAttr_t communicateTask_attributes = {
  .name = "communicateTask",
  .stack_size = 256 * 4,
  .priority = (osPriority_t) osPriorityAboveNormal,
};
/* Definitions for motorTask */
osThreadId_t motorTaskHandle;
const osThreadAttr_t motorTask_attributes = {
  .name = "motorTask",
  .stack_size = 256 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};
/* Definitions for oledTask */
osThreadId_t oledTaskHandle;
const osThreadAttr_t oledTask_attributes = {
  .name = "oledTask",
  .stack_size = 512 * 4,
  .priority = (osPriority_t) osPriorityLow,
};
/* Definitions for gyroTask */
osThreadId_t gyroTaskHandle;
const osThreadAttr_t gyroTask_attributes = {
  .name = "gyroTask",
  .stack_size = 256 * 4,
  .priority = (osPriority_t) osPriorityAboveNormal,
};
/* Definitions for ultrasonicTask */
osThreadId_t ultrasonicTaskHandle;
const osThreadAttr_t ultrasonicTask_attributes = {
  .name = "ultrasonicTask",
  .stack_size = 256 * 4,
  .priority = (osPriority_t) osPriorityBelowNormal,
};
/* Definitions for uartQueue */
osMessageQueueId_t uartQueueHandle;
const osMessageQueueAttr_t uartQueue_attributes = {
  .name = "uartQueue"
};
/* USER CODE BEGIN PV */

// Global variables safely accessed by FreeRTOS tasks
/* --- SERIAL COMMUNICATION VARIABLES --- */
uint8_t rxByte;									// UART receive buffer
int flag_done = 0;
int magnitude = 0;

/* --- MOTOR VARIABLES --- */
uint16_t pwmVal_servo = SERVOCENTER;
uint16_t pwmVal_R = 0;
uint16_t pwmVal_L = 0;
int times_acceptable = 0;
int e_brake = 0;

/* --- ENCODER VARIABLES --- */
int32_t left_encoder_val = 0;
int32_t right_encoder_val = 0;
int32_t left_target = 0;
int32_t right_target = 0;
double target_angle = 0;

/* --- GYROSCOPE VARIABLES --- */
double total_angle = 0;
uint8_t gyroBuffer[20];
uint8_t ICMAddress = 0x68;
double error_angle = 0;

/* --- OLED DISPLAY VARIABLES --- */
char oled_status_msg[32] = "Booting...";		// For boot/status message
char dash_lastCmd[15] = "None";					// RPi command
double dash_gyroZ = 0.0;						// Gyroscope angle
float dash_ultraDist = 0.0;						// Ultrasonic distance
int dash_encoderL = 0;							// Encoder Left Speed
int dash_encoderR = 0;							// Encoder Right Speed
uint16_t dash_direction = 0;					// Robot direction?
uint32_t diag_timer = 0;

/* --- ULTRASONIC VARIABLES --- */
uint32_t tc1 = 0;
uint32_t tc2 = 0;
uint32_t echo = 0;
uint8_t first_captured = 0;
uint16_t distance = 0;
int k = 0;

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_USART3_UART_Init(void);
static void MX_TIM6_Init(void);
static void MX_I2C2_Init(void);
static void MX_TIM5_Init(void);
static void MX_TIM4_Init(void);
static void MX_TIM9_Init(void);
static void MX_TIM2_Init(void);
static void MX_TIM3_Init(void);
static void MX_TIM12_Init(void);
void StartDefaultTask(void *argument);
void StartCommunicateTask(void *argument);
void StartMotorTask(void *argument);
void StartOledTask(void *argument);
void StartGyroTask(void *argument);
void StartUltrasonicTask(void *argument);

/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{
  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_USART3_UART_Init();
  MX_TIM6_Init();
  MX_I2C2_Init();
  MX_TIM5_Init();
  MX_TIM4_Init();
  MX_TIM9_Init();
  MX_TIM2_Init();
  MX_TIM3_Init();
  MX_TIM12_Init();
  /* USER CODE BEGIN 2 */
  OLED_Init();
  strcpy(oled_status_msg, "System Init OK");
  osDelay(500);

  // Initialize IMU
  if (ICM20948_Init() == HAL_OK)
  {
	  strcpy(oled_status_msg, "IMU Found");
	  osDelay(500);
  } else {
	  strcpy(oled_status_msg, "IMU ERROR!");
	  while(1);
  }

  // Start UART Interrupt Listener
  HAL_UART_Receive_IT(&huart3, &rxByte, 1);

  // Start Ultrasonic Timers
  HAL_TIM_Base_Start(&htim6);
  HAL_TIM_IC_Start_IT(&htim5, TIM_CHANNEL_3);
  /* USER CODE END 2 */

  /* Init scheduler */
  osKernelInitialize();

  /* USER CODE BEGIN RTOS_MUTEX */
  /* add mutexes, ... */
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* add semaphores, ... */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* Create the queue(s) */
  /* creation of uartQueue */
  uartQueueHandle = osMessageQueueNew (32, sizeof(uint8_t), &uartQueue_attributes);

  /* USER CODE BEGIN RTOS_QUEUES */
  /* add queues, ... */
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* creation of defaultTask */
  defaultTaskHandle = osThreadNew(StartDefaultTask, NULL, &defaultTask_attributes);

  /* creation of communicateTask */
  communicateTaskHandle = osThreadNew(StartCommunicateTask, NULL, &communicateTask_attributes);

  /* creation of motorTask */
  motorTaskHandle = osThreadNew(StartMotorTask, NULL, &motorTask_attributes);

  /* creation of oledTask */
  oledTaskHandle = osThreadNew(StartOledTask, NULL, &oledTask_attributes);

  /* creation of gyroTask */
  gyroTaskHandle = osThreadNew(StartGyroTask, NULL, &gyroTask_attributes);

  /* creation of ultrasonicTask */
  ultrasonicTaskHandle = osThreadNew(StartUltrasonicTask, NULL, &ultrasonicTask_attributes);

  /* USER CODE BEGIN RTOS_THREADS */
  /* add threads, ... */
  /* USER CODE END RTOS_THREADS */

  /* USER CODE BEGIN RTOS_EVENTS */
  /* add events, ... */
  /* USER CODE END RTOS_EVENTS */

  /* Start scheduler */
  osKernelStart();

  /* We should never get here as control is now taken by the scheduler */
  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief I2C2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_I2C2_Init(void)
{

  /* USER CODE BEGIN I2C2_Init 0 */

  /* USER CODE END I2C2_Init 0 */

  /* USER CODE BEGIN I2C2_Init 1 */

  /* USER CODE END I2C2_Init 1 */
  hi2c2.Instance = I2C2;
  hi2c2.Init.ClockSpeed = 100000;
  hi2c2.Init.DutyCycle = I2C_DUTYCYCLE_2;
  hi2c2.Init.OwnAddress1 = 0;
  hi2c2.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c2.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c2.Init.OwnAddress2 = 0;
  hi2c2.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c2.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN I2C2_Init 2 */

  /* USER CODE END I2C2_Init 2 */

}

/**
  * @brief TIM2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM2_Init(void)
{

  /* USER CODE BEGIN TIM2_Init 0 */

  /* USER CODE END TIM2_Init 0 */

  TIM_Encoder_InitTypeDef sConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};

  /* USER CODE BEGIN TIM2_Init 1 */

  /* USER CODE END TIM2_Init 1 */
  htim2.Instance = TIM2;
  htim2.Init.Prescaler = 0;
  htim2.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim2.Init.Period = 65535;
  htim2.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  sConfig.EncoderMode = TIM_ENCODERMODE_TI12;
  sConfig.IC1Polarity = TIM_ICPOLARITY_RISING;
  sConfig.IC1Selection = TIM_ICSELECTION_DIRECTTI;
  sConfig.IC1Prescaler = TIM_ICPSC_DIV1;
  sConfig.IC1Filter = 10;
  sConfig.IC2Polarity = TIM_ICPOLARITY_RISING;
  sConfig.IC2Selection = TIM_ICSELECTION_DIRECTTI;
  sConfig.IC2Prescaler = TIM_ICPSC_DIV1;
  sConfig.IC2Filter = 10;
  if (HAL_TIM_Encoder_Init(&htim2, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim2, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM2_Init 2 */

  /* USER CODE END TIM2_Init 2 */

}

/**
  * @brief TIM3 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM3_Init(void)
{

  /* USER CODE BEGIN TIM3_Init 0 */

  /* USER CODE END TIM3_Init 0 */

  TIM_Encoder_InitTypeDef sConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};

  /* USER CODE BEGIN TIM3_Init 1 */

  /* USER CODE END TIM3_Init 1 */
  htim3.Instance = TIM3;
  htim3.Init.Prescaler = 0;
  htim3.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim3.Init.Period = 65535;
  htim3.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim3.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  sConfig.EncoderMode = TIM_ENCODERMODE_TI12;
  sConfig.IC1Polarity = TIM_ICPOLARITY_FALLING;
  sConfig.IC1Selection = TIM_ICSELECTION_DIRECTTI;
  sConfig.IC1Prescaler = TIM_ICPSC_DIV1;
  sConfig.IC1Filter = 10;
  sConfig.IC2Polarity = TIM_ICPOLARITY_RISING;
  sConfig.IC2Selection = TIM_ICSELECTION_DIRECTTI;
  sConfig.IC2Prescaler = TIM_ICPSC_DIV1;
  sConfig.IC2Filter = 10;
  if (HAL_TIM_Encoder_Init(&htim3, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim3, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM3_Init 2 */

  /* USER CODE END TIM3_Init 2 */

}

/**
  * @brief TIM4 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM4_Init(void)
{

  /* USER CODE BEGIN TIM4_Init 0 */

  /* USER CODE END TIM4_Init 0 */

  TIM_ClockConfigTypeDef sClockSourceConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};
  TIM_OC_InitTypeDef sConfigOC = {0};

  /* USER CODE BEGIN TIM4_Init 1 */

  /* USER CODE END TIM4_Init 1 */
  htim4.Instance = TIM4;
  htim4.Init.Prescaler = 0;
  htim4.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim4.Init.Period = 7199;
  htim4.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim4.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
  if (HAL_TIM_Base_Init(&htim4) != HAL_OK)
  {
    Error_Handler();
  }
  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  if (HAL_TIM_ConfigClockSource(&htim4, &sClockSourceConfig) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_PWM_Init(&htim4) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim4, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sConfigOC.OCMode = TIM_OCMODE_PWM1;
  sConfigOC.Pulse = 0;
  sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
  sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
  if (HAL_TIM_PWM_ConfigChannel(&htim4, &sConfigOC, TIM_CHANNEL_3) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_PWM_ConfigChannel(&htim4, &sConfigOC, TIM_CHANNEL_4) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM4_Init 2 */

  /* USER CODE END TIM4_Init 2 */
  HAL_TIM_MspPostInit(&htim4);

}

/**
  * @brief TIM5 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM5_Init(void)
{

  /* USER CODE BEGIN TIM5_Init 0 */

  /* USER CODE END TIM5_Init 0 */

  TIM_ClockConfigTypeDef sClockSourceConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};
  TIM_IC_InitTypeDef sConfigIC = {0};

  /* USER CODE BEGIN TIM5_Init 1 */

  /* USER CODE END TIM5_Init 1 */
  htim5.Instance = TIM5;
  htim5.Init.Prescaler = 16-1;
  htim5.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim5.Init.Period = 65535;
  htim5.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim5.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim5) != HAL_OK)
  {
    Error_Handler();
  }
  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  if (HAL_TIM_ConfigClockSource(&htim5, &sClockSourceConfig) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_IC_Init(&htim5) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim5, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sConfigIC.ICPolarity = TIM_INPUTCHANNELPOLARITY_RISING;
  sConfigIC.ICSelection = TIM_ICSELECTION_DIRECTTI;
  sConfigIC.ICPrescaler = TIM_ICPSC_DIV1;
  sConfigIC.ICFilter = 0;
  if (HAL_TIM_IC_ConfigChannel(&htim5, &sConfigIC, TIM_CHANNEL_3) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM5_Init 2 */

  /* USER CODE END TIM5_Init 2 */

}

/**
  * @brief TIM6 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM6_Init(void)
{

  /* USER CODE BEGIN TIM6_Init 0 */

  /* USER CODE END TIM6_Init 0 */

  TIM_MasterConfigTypeDef sMasterConfig = {0};

  /* USER CODE BEGIN TIM6_Init 1 */

  /* USER CODE END TIM6_Init 1 */
  htim6.Instance = TIM6;
  htim6.Init.Prescaler = 16-1;
  htim6.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim6.Init.Period = 65535;
  htim6.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim6) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim6, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM6_Init 2 */

  /* USER CODE END TIM6_Init 2 */

}

/**
  * @brief TIM9 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM9_Init(void)
{

  /* USER CODE BEGIN TIM9_Init 0 */

  /* USER CODE END TIM9_Init 0 */

  TIM_ClockConfigTypeDef sClockSourceConfig = {0};
  TIM_OC_InitTypeDef sConfigOC = {0};

  /* USER CODE BEGIN TIM9_Init 1 */

  /* USER CODE END TIM9_Init 1 */
  htim9.Instance = TIM9;
  htim9.Init.Prescaler = 0;
  htim9.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim9.Init.Period = 7199;
  htim9.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim9.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
  if (HAL_TIM_Base_Init(&htim9) != HAL_OK)
  {
    Error_Handler();
  }
  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  if (HAL_TIM_ConfigClockSource(&htim9, &sClockSourceConfig) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_PWM_Init(&htim9) != HAL_OK)
  {
    Error_Handler();
  }
  sConfigOC.OCMode = TIM_OCMODE_PWM1;
  sConfigOC.Pulse = 0;
  sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
  sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
  if (HAL_TIM_PWM_ConfigChannel(&htim9, &sConfigOC, TIM_CHANNEL_1) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_PWM_ConfigChannel(&htim9, &sConfigOC, TIM_CHANNEL_2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM9_Init 2 */

  /* USER CODE END TIM9_Init 2 */
  HAL_TIM_MspPostInit(&htim9);

}

/**
  * @brief TIM12 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM12_Init(void)
{

  /* USER CODE BEGIN TIM12_Init 0 */

  /* USER CODE END TIM12_Init 0 */

  TIM_ClockConfigTypeDef sClockSourceConfig = {0};
  TIM_OC_InitTypeDef sConfigOC = {0};

  /* USER CODE BEGIN TIM12_Init 1 */

  /* USER CODE END TIM12_Init 1 */
  htim12.Instance = TIM12;
  htim12.Init.Prescaler = 160-1;
  htim12.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim12.Init.Period = 2000-1;
  htim12.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim12.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
  if (HAL_TIM_Base_Init(&htim12) != HAL_OK)
  {
    Error_Handler();
  }
  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  if (HAL_TIM_ConfigClockSource(&htim12, &sClockSourceConfig) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_PWM_Init(&htim12) != HAL_OK)
  {
    Error_Handler();
  }
  sConfigOC.OCMode = TIM_OCMODE_PWM1;
  sConfigOC.Pulse = 0;
  sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
  sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
  if (HAL_TIM_PWM_ConfigChannel(&htim12, &sConfigOC, TIM_CHANNEL_2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM12_Init 2 */

  /* USER CODE END TIM12_Init 2 */
  HAL_TIM_MspPostInit(&htim12);

}

/**
  * @brief USART3 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART3_UART_Init(void)
{

  /* USER CODE BEGIN USART3_Init 0 */

  /* USER CODE END USART3_Init 0 */

  /* USER CODE BEGIN USART3_Init 1 */

  /* USER CODE END USART3_Init 1 */
  huart3.Instance = USART3;
  huart3.Init.BaudRate = 115200;
  huart3.Init.WordLength = UART_WORDLENGTH_8B;
  huart3.Init.StopBits = UART_STOPBITS_1;
  huart3.Init.Parity = UART_PARITY_NONE;
  huart3.Init.Mode = UART_MODE_TX_RX;
  huart3.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart3.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart3) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART3_Init 2 */

  /* USER CODE END USART3_Init 2 */

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOE_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(ULTRASONIC_TRIG_GPIO_Port, ULTRASONIC_TRIG_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(LED3_GPIO_Port, LED3_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOD, OLED_DC_Pin|OLED_RESET__Pin|OLED_SDIN_Pin|OLED_SCLK_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin : ULTRASONIC_TRIG_Pin */
  GPIO_InitStruct.Pin = ULTRASONIC_TRIG_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(ULTRASONIC_TRIG_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : LED3_Pin */
  GPIO_InitStruct.Pin = LED3_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(LED3_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : IMU_INT_Pin */
  GPIO_InitStruct.Pin = IMU_INT_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(IMU_INT_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : OLED_DC_Pin OLED_RESET__Pin OLED_SDIN_Pin OLED_SCLK_Pin */
  GPIO_InitStruct.Pin = OLED_DC_Pin|OLED_RESET__Pin|OLED_SDIN_Pin|OLED_SCLK_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);

}

/* USER CODE BEGIN 4 */
// Serial Communication Interrupt Callback
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart) {
	/* Prevent unused argument(s) compilation warning */
	if (huart -> Instance == USART3)
	{
		osMessageQueuePut(uartQueueHandle, &rxByte, 0U, 0U);
		HAL_UART_Receive_IT(&huart3, &rxByte, 1);
	}
}

// Ultrasonic Microsecond Delay
void delay_us(uint16_t us) {
	__HAL_TIM_SET_COUNTER(&htim6, 0);
	while(__HAL_TIM_GET_COUNTER(&htim6) < us);
}

// Ultrasonic Input Capture Callback
void HAL_TIM_IC_CaptureCallback(TIM_HandleTypeDef *htim) {
	if (htim->Instance == TIM5 && htim->Channel == HAL_TIM_ACTIVE_CHANNEL_3) {
		if (first_captured == 0) {
			tc1 = HAL_TIM_ReadCapturedValue(htim, TIM_CHANNEL_3);
			first_captured = 1;
			__HAL_TIM_SET_CAPTUREPOLARITY(htim, TIM_CHANNEL_3, TIM_INPUTCHANNELPOLARITY_FALLING);
		}
		else if (first_captured == 1) {
			tc2 = HAL_TIM_ReadCapturedValue(htim, TIM_CHANNEL_3);
			__HAL_TIM_SET_COUNTER(htim, 0);

			if (tc2 > tc1)
				echo = tc2 - tc1;
			else
				echo = (65535 - tc1) + tc2;

			// Update ultrasonic distance
			dash_ultraDist = (echo * 0.0343 / 2) + 1;

			first_captured = 0;

			__HAL_TIM_SET_CAPTUREPOLARITY(htim, TIM_CHANNEL_3, TIM_INPUTCHANNELPOLARITY_RISING);
			__HAL_TIM_DISABLE_IT(htim, TIM_IT_CC3);
		}
	}
}

int PID_Angle(double errord) {
	// 1. Calculate the error
	int error = (int)(errord * 10);

	// 2. Get absolute value
	error = abs(error);

	// 3. Return PWM Magnitude (stepped proportional control)
	if (error > 300) return PID_ANG_MAX;
	else if (error > 200) return PID_ANG_HIGH;
	else if (error > 150) return PID_ANG_MED;
	else if (error > 100) return PID_ANG_LOW;
	else if (error > 10) return PID_ANG_FINE;
	else if (error >= 2) {
		times_acceptable++;
		return PID_ANG_MIN;
	} else {
		times_acceptable++;
		return 0;
	}
}

int PID_Control(int error) {
    // 1. Get the absolute error since direction is handled in StartMotorTask
    error = abs(error);

    // 2. Return the stepped proportional PWM speed magnitude based on your senior's tuned steps
    if (error > 2000) return PID_STR_MAX;
    else if (error > 500) return PID_STR_HIGH;
    else if (error > 200) return PID_STR_MED;
    else if (error > 100) return PID_STR_LOW;
    else if (error > 2) {
        times_acceptable++;
        return PID_STR_MIN;
    } else if (error >= 1) {
    	times_acceptable++;
        return 0;
    } else {
        times_acceptable++;
        return 0;
    }
}

int finishCheck() {
    // If PID error has been minimal for ~20 ticks (approx 200ms)
    if (times_acceptable > 20) {
        e_brake = 1; // Signal motor task to cut PWM
        times_acceptable = 0;
        pwmVal_servo = SERVOCENTER;
        osDelay(300); // Wait for servo to physically recenter
        return 0; // 0 means "Finished"
    }
    return 1; // 1 means "Still moving"
}

void moveCarStraight(double distance) {
    // Convert cm to encoder ticks (75 is a calibration multiplier you will tune later)
    int tick_distance = (int)(distance * TICKS_PER_CM);

    pwmVal_servo = SERVOCENTER;
    osDelay(300); // Allow physical servo to center itself

    e_brake = 0;
    times_acceptable = 0;

    // Set a high baseline to prevent negative underflow
    left_encoder_val = 75000;
    right_encoder_val = 75000;
    left_target = 75000 + tick_distance;
    right_target = 75000 + tick_distance;

    // Wait until the robot reaches the target
    while (finishCheck()) {
        osDelay(10); // CRITICAL: Yields CPU so StartMotorTask can actually run!
    }
}

void moveCarRight(double angle) {
    pwmVal_servo = SERVORIGHT;
    osDelay(300);

    e_brake = 0;
    times_acceptable = 0;

    // Subtract from target angle (assuming right turn decreases Z-axis angle)
    target_angle -= angle;

    while (finishCheck()) {
        osDelay(10);
    }
}

void moveCarLeft(double angle) {
    pwmVal_servo = SERVOLEFT;
    osDelay(300);

    e_brake = 0;
    times_acceptable = 0;

    // Add to target angle (assuming left turn increases Z-axis angle)
    target_angle += angle;

    while (finishCheck()) {
        osDelay(10);
    }
}

void moveCarSlideRight(int forward) {
    int sign = (forward == 1) ? 1 : -1;
    e_brake = 0;
    times_acceptable = 0;

    // Step 1: Drive Straight to clear obstacle
    if (sign > 0) {
        moveCarStraight((450.0 / SLIDE_TICKS_PER_CM) * sign);
    } else {
        moveCarStraight((540.0 / SLIDE_TICKS_PER_CM) * sign);
    }

    // Wait for the straight segment to finish
    while (finishCheck()) {
        osDelay(10);
    }
    osDelay(50); // Small pause to stabilize physical inertia
    times_acceptable = 0;

    // Step 2: Turn Right
    moveCarRight(29.0 * sign);
    while (finishCheck()) {
        osDelay(10);
    }
    osDelay(50);
    times_acceptable = 0;

    // Step 3: Counter-steer Left to re-align heading
    moveCarLeft(29.0 * sign);
    while (finishCheck()) {
        osDelay(10);
    }
    osDelay(50);
}

void moveCarSlideLeft(int forward) {
    int sign = (forward == 1) ? 1 : -1;
    e_brake = 0;
    times_acceptable = 0;

    // Step 1: Drive Straight to clear obstacle
    if (sign > 0) {
        moveCarStraight((560.0 / SLIDE_TICKS_PER_CM) * sign);
    } else {
        moveCarStraight((700.0 / SLIDE_TICKS_PER_CM) * sign);
    }

    // Wait for the straight segment to finish
    while (finishCheck()) {
        osDelay(10);
    }
    osDelay(50);
    times_acceptable = 0;

    // Step 2: Turn Left
    moveCarLeft(29.0 * sign);
    while (finishCheck()) {
        osDelay(10);
    }
    osDelay(50);
    times_acceptable = 0;

    // Step 3: Counter-steer Right to re-align heading
    moveCarRight(29.0 * sign);
    while (finishCheck()) {
        osDelay(10);
    }
    osDelay(50);
}


/* USER CODE END 4 */

/* USER CODE BEGIN Header_StartDefaultTask */
/**
  * @brief  Function implementing the defaultTask thread.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_StartDefaultTask */
void StartDefaultTask(void *argument)
{
  /* USER CODE BEGIN 5 */
  //char debugBuf[100];

  /* Infinite loop */
  for(;;)
  {
	HAL_GPIO_TogglePin(LED3_GPIO_Port, LED3_Pin);
	//int len = sprintf(debugBuf, "Time: %lu ms | EncL: %d | EncR: %d\r\n", diag_timer, dash_encoderL, dash_encoderR);
	//HAL_UART_Transmit(&huart3, (uint8_t*)debugBuf, len, HAL_MAX_DELAY);
    osDelay(1000);
  }
  /* USER CODE END 5 */
}

/* USER CODE BEGIN Header_StartCommunicateTask */
/**
* @brief Function implementing the communicateTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartCommunicateTask */
void StartCommunicateTask(void *argument)
{
  /* USER CODE BEGIN StartCommunicateTask */

  char cmdBuffer[15];		// Local buffer to hold the built string (e.g. "L90")
  uint8_t receivedChar;
  uint8_t cmdIndex = 0;

  /* Infinite loop */
  for(;;)
  {
	// Wait until a character is put into the queue
	if (osMessageQueueGet(uartQueueHandle, &receivedChar, NULL, osWaitForever) == osOK)
	{
		// Check for newline, because RPi will send command ending with newline '\n' or '\r'
		if (receivedChar == '\n' || receivedChar == '\r')
		{
			if (cmdIndex > 0)
			{
				cmdBuffer[cmdIndex] = '\0'; // Add null-terminator to make a valid C-string

				// 1. PARSE THE COMMAND
				char command_char1 = ' ';
				char command_char2 = ' ';
				int value = 0;
				int items_parsed = 0;

				// Check if the second character (index 1) is a letter (A-Z or a-z)
				// We use cmdBuffer because index 0 is the first letter (e.g., 'F' or 'S')
				if ((cmdBuffer[1] >= 'A' && cmdBuffer[1] <= 'Z') || (cmdBuffer[1] >= 'a' && cmdBuffer[1] <= 'z'))
				{
					// It's a 2-letter command like "SL50"
					items_parsed = sscanf(cmdBuffer, "%c%c%d", &command_char1, &command_char2, &value);
				}
				else
				{
					// It's a 1-letter command like "F50"
					items_parsed = sscanf(cmdBuffer, "%c%d", &command_char1, &value);
				}

				// 2. EXECUTE THE MOVEMENT FUNCTION
				if (items_parsed > 0)
				{
					switch (command_char1)
					{
						case 'S': 							// Straight or Slide
							if (command_char2 == 'L') 		// "SL" command for Slide Left
								moveCarSlideLeft(value); 	// value is 1 for fwd, -1 for bwd
							else if (command_char2 == 'R') 	// "SR" command for Slide Right
								moveCarSlideRight(value);
							else
								moveCarStraight(value);		// "S" command for Straight
							break;

						case 'F':							// Forward (can be an alias for Straight)
							moveCarStraight(value);
							break;

						case 'B':							// Backward (can be an alias for Straight with negative value
							moveCarStraight(-value);
							break;

						case 'R':							// Right Turn
							moveCarRight(value);
							break;

						case 'L':							// Left Turn
							moveCarLeft(value);
							break;

						/* Can add more cases here */
					}
				}

				// 3. SEND ACK TO RPI
				uint8_t ackMsg[] = "A\n";
				HAL_UART_Transmit(&huart3, ackMsg, sizeof(ackMsg)-1, 100);

				// 4. UPDATE OLED RPI COMMAND VARIABLE & RESET
				strcpy(dash_lastCmd, cmdBuffer);
				cmdIndex = 0; // Reset command index to 0 to prepare for next command
			}
		} else {
			// This means not a newline yet, continue adding character into cmdBuffer
			// Check for command length, ensure don't overflow the 15-char array limit
			if (cmdIndex < 14)
			{
				cmdBuffer[cmdIndex] = receivedChar;
				cmdIndex++;
			}
		}
	}
  }
  /* USER CODE END StartCommunicateTask */
}

/* USER CODE BEGIN Header_StartMotorTask */
/**
* @brief Function implementing the motorTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartMotorTask */
void StartMotorTask(void *argument)
{
  /* USER CODE BEGIN StartMotorTask */

  // Encoder Variables
  int16_t cnt_L = 0;
  int16_t cnt_R = 0;
  int straight_correction = 0;
  pwmVal_L = 0;
  pwmVal_R = 0;
  left_encoder_val = 0;
  right_encoder_val = 0;

  // Start Encoder Timer
  HAL_TIM_Encoder_Start(&htim2, TIM_CHANNEL_ALL); // Motor A (Left) Encoder
  HAL_TIM_Encoder_Start(&htim3, TIM_CHANNEL_ALL); // Motor B (Right) Encoder

  // Start PWM Timer for Motor A (Left, PB8 & PB9 via TIM4)
  HAL_TIM_PWM_Start(&htim4, TIM_CHANNEL_3);
  HAL_TIM_PWM_Start(&htim4, TIM_CHANNEL_4);

  // Start PWM Timer for Motor B (Right, PE5 & PE6 via TIM9)
  HAL_TIM_PWM_Start(&htim9, TIM_CHANNEL_1);
  HAL_TIM_PWM_Start(&htim9, TIM_CHANNEL_2);

  // Start PWM Timer for Servo Motor (PB15 vis TIM12)
  HAL_TIM_PWM_Start(&htim12, TIM_CHANNEL_2);

  // Ensure robot starts completely stationary and centered
  __HAL_TIM_SET_COMPARE(&htim12, TIM_CHANNEL_2, SERVOCENTER); // Center Servo (1.5ms pulse)
  __HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_3, 0);
  __HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_4, 0);
  __HAL_TIM_SET_COMPARE(&htim9, TIM_CHANNEL_1, 0);
  __HAL_TIM_SET_COMPARE(&htim9, TIM_CHANNEL_2, 0);

  osDelay(1000);

  // Reset Encoder Count
  __HAL_TIM_SET_COUNTER(&htim2, 0);
  __HAL_TIM_SET_COUNTER(&htim3, 0);

  /* Infinite loop */
  for(;;)
  {
	// Step A: Read Encoders
	cnt_L = (int16_t)__HAL_TIM_GET_COUNTER(&htim2);
	cnt_R = (int16_t)__HAL_TIM_GET_COUNTER(&htim3);

	__HAL_TIM_SET_COUNTER(&htim2, 0);
	__HAL_TIM_SET_COUNTER(&htim3, 0);

	left_encoder_val += cnt_L;
	right_encoder_val += cnt_R;

	dash_encoderL = left_encoder_val;
	dash_encoderR = right_encoder_val;
	// Speed calibration
	//dash_encoderL = cnt_L;
	//dash_encoderR = cnt_R;
	dash_direction = __HAL_TIM_IS_TIM_COUNTING_DOWN(&htim2);

	// Step B: Move Servo Motor
	__HAL_TIM_SET_COMPARE(&htim12, TIM_CHANNEL_2, pwmVal_servo);
	error_angle = target_angle - total_angle;

	if (pwmVal_servo < TURNLEFT_TH) // Turn left
	{
		// 1. Calculate base speeds
		pwmVal_R = PID_Angle(error_angle) * RIGHT_MOTOR_BIAS;	// Master Wheel: Right
		pwmVal_L = pwmVal_R * TURN_SLAVE_RATIO;					// Slave Wheel: Left

		// 2. Apply speeds to 2-pin H-Bridge based on error direction
		if (error_angle > 0) {
			// --- Forward Movement ---
			// Left Motor (TIM4) Forward
			__HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_3, 0);
			__HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_4, pwmVal_L);

			// Right Motor (TIM9) Forward
            __HAL_TIM_SET_COMPARE(&htim9, TIM_CHANNEL_1, 0);
            __HAL_TIM_SET_COMPARE(&htim9, TIM_CHANNEL_2, pwmVal_R);
		} else {
			// --- Backward Movement ---
			// Left Motor (TIM4) Backward
			__HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_3, pwmVal_L);
			__HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_4, 0);

			// Right Motor (TIM9) Backward
            __HAL_TIM_SET_COMPARE(&htim9, TIM_CHANNEL_1, pwmVal_R);
            __HAL_TIM_SET_COMPARE(&htim9, TIM_CHANNEL_2, 0);
		}
	}

	else if (pwmVal_servo > TURNRIGHT_TH) // Turn right
	{
		pwmVal_L = PID_Angle(error_angle); // Master Wheel: Left
		pwmVal_R = pwmVal_L * TURN_SLAVE_RATIO;	   // Slave Wheel: Right

		// 2. Apply speeds to 2-pin H-Bridge based on error direction
		if (error_angle < 0) {
			// --- Forward Movement ---
			// Left Motor (TIM4) Forward
			__HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_3, 0);
			__HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_4, pwmVal_L);

			// Right Motor (TIM9) Forward
            __HAL_TIM_SET_COMPARE(&htim9, TIM_CHANNEL_1, 0);
            __HAL_TIM_SET_COMPARE(&htim9, TIM_CHANNEL_2, pwmVal_R);
		} else {
			// --- Backward Movement ---
			// Left Motor (TIM4) Backward
			__HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_3, pwmVal_L);
			__HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_4, 0);

			// Right Motor (TIM9) Backward
            __HAL_TIM_SET_COMPARE(&htim9, TIM_CHANNEL_1, pwmVal_R);
            __HAL_TIM_SET_COMPARE(&htim9, TIM_CHANNEL_2, 0);
		}
	}

	else // Straight
	{
		// 1. Calculate base speeds (Master = Right Motor, Slave = Left Motor)
		pwmVal_R = PID_Control(right_target - right_encoder_val) * RIGHT_MOTOR_BIAS;

		// 2. Perform drift compensation (straightCorrection)
		if (abs(left_target - left_encoder_val) > abs(right_target - right_encoder_val))
			straight_correction++;
		else
			straight_correction--;

		// Reset correction if close to target to avoid oscillating
		if (abs(left_target - left_encoder_val) < 100)
			straight_correction = 0;

		pwmVal_L = PID_Control(left_target - left_encoder_val) + straight_correction; // Slave wheel (TIM4)

		// 3. Servo Fine-Tuning (Micro-steering using Gyro to stay on course)
		int error_sign = (right_target - right_encoder_val < 0) ? -1 : 1;

		if (error_angle > 5)
			pwmVal_servo = (error_sign * -19 * 5) / 5 + SERVOCENTER;
		else if (error_angle < -5)
			pwmVal_servo = (error_sign * 19 * 5) / 5 + SERVOCENTER;
		else
			pwmVal_servo = (error_sign * -19 * error_angle) /5 + SERVOCENTER;

		// Write micro-adjustments to TIM12 servo
		__HAL_TIM_SET_COMPARE(&htim12, TIM_CHANNEL_2, pwmVal_servo);

		// 4. Apply speed to 2-Pin Dual-PWM H-Bridge based on Direction
		// Check if moving forward or backward based on target direction
		if((right_target - right_encoder_val) > 0) {
			// --- Forward Movement ---
			// Left Motor (TIM4) Forward
			__HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_3, 0);
			__HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_4, pwmVal_L);

			// Right Motor (TIM9) Forward
            __HAL_TIM_SET_COMPARE(&htim9, TIM_CHANNEL_1, 0);
            __HAL_TIM_SET_COMPARE(&htim9, TIM_CHANNEL_2, pwmVal_R);
		} else {
			// --- Backward Movement ---
			// Left Motor (TIM4) Backward
			__HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_3, pwmVal_L);
			__HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_4, 0);

			// Right Motor (TIM9) Backward
			__HAL_TIM_SET_COMPARE(&htim9, TIM_CHANNEL_1, pwmVal_R);
			__HAL_TIM_SET_COMPARE(&htim9, TIM_CHANNEL_2, 0);
		}

	}

	if (e_brake) {
		pwmVal_L = 0;
		pwmVal_R = 0;
		left_target = left_encoder_val;
		right_target = right_encoder_val;

        // Instantly cut PWM to your 2-pin setup
        __HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_3, 0);
        __HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_4, 0);
        __HAL_TIM_SET_COMPARE(&htim9, TIM_CHANNEL_1, 0);
        __HAL_TIM_SET_COMPARE(&htim9, TIM_CHANNEL_2, 0);
	}

	osDelay(10);

	if (times_acceptable > 1000)
		times_acceptable = 1001;
  }
  /* USER CODE END StartMotorTask */
}

/* USER CODE BEGIN Header_StartOledTask */
/**
* @brief Function implementing the oledTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartOledTask */
void StartOledTask(void *argument)
{
  /* USER CODE BEGIN StartOledTask */
  char textBuffer[32];	// Temp buffer to format numbers into text

  /* Infinite loop */
  for(;;)
  {
	OLED_Clear();

	if (strlen(oled_status_msg) > 0)
	{
		OLED_ShowString(0, 24, (uint8_t*)oled_status_msg);
	}
	else
	{
		// Line 1: RPi Instructions
		OLED_ShowString(0, 0, (uint8_t*) "CMD: ");
		OLED_ShowString(35, 0, (uint8_t*) dash_lastCmd);

		// Line 2: Gyroscope Angle & Ultrasonic Distance
		sprintf(textBuffer, "G: %.1f D: %.1fcm", (float)dash_gyroZ, dash_ultraDist);
		OLED_ShowString(0, 12, (uint8_t*) textBuffer);

		// Line 3: Left Encoder Value
		sprintf(textBuffer, "Enc L: %d", dash_encoderL);
		OLED_ShowString(0, 24, (uint8_t *) textBuffer);

		// Line 4: Right Encoder Value
		sprintf(textBuffer, "Enc R: %d", dash_encoderR);
		OLED_ShowString(0, 36, (uint8_t *) textBuffer);

		// Line 5: Encoder Direction
		sprintf(textBuffer, "Dir: %d", dash_direction);
		OLED_ShowString(0, 48, (uint8_t *) textBuffer);
	}

	OLED_Refresh_Gram();
    osDelay(100);
  }
  /* USER CODE END StartOledTask */
}

/* USER CODE BEGIN Header_StartGyroTask */
/**
* @brief Function implementing the gyroTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartGyroTask */
void StartGyroTask(void *argument)
{
  /* USER CODE BEGIN StartGyroTask */
  ICM20948_Data IMU_Data;
  double offset = 0.0;
  uint32_t tick = 0;
  int i = 0;

  // 1. START-UP CALIBRATION PHASE
  // KEEP ROBOT STILL DURING FIRST 5 SECONDS!
  strcpy(oled_status_msg, "Calibrating Gyro...");
  osDelay(500);

  while (i < 100)
  {
	  osDelay(30); // Get sample every 30ms
	  ICM_ReadData(&IMU_Data);
	  offset += (double)IMU_Data.z_gyro;
	  i++;
  }
  offset = offset / 100.0; // Calculate average drift bias

  // Show on OLED that calibration is complete
  strcpy(oled_status_msg, "Calib Done!");
  osDelay(500);
  strcpy(oled_status_msg, "");

  tick = HAL_GetTick();

  /* Infinite loop */
  for(;;)
  {
    osDelay(10); // High-frequency 100Hz loop for accurate Euler integration

	// 1. Fetch IMU data
	ICM_ReadData(&IMU_Data);

	// 2. Calculate time elapsed (delta t) in seconds
	uint32_t current_tick = HAL_GetTick();
	double dt = (double)(current_tick - tick) / 1000.0;
	tick = current_tick;

	// 3. Subtract baseline offset and integrate angular velocity to get absolute degrees
	total_angle += ((double)IMU_Data.z_gyro - offset) * dt;

	// 4. Update gyro dashboard variable
	dash_gyroZ = total_angle;
  }
  /* USER CODE END StartGyroTask */
}

/* USER CODE BEGIN Header_StartUltrasonicTask */
/**
* @brief Function implementing the ultrasonicTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartUltrasonicTask */
void StartUltrasonicTask(void *argument)
{
  /* USER CODE BEGIN StartUltrasonicTask */
  /* Infinite loop */
  for(;;)
  {
	HAL_GPIO_WritePin(ULTRASONIC_TRIG_GPIO_Port, ULTRASONIC_TRIG_Pin, GPIO_PIN_RESET);
	delay_us(2);

	first_captured = 0;
	__HAL_TIM_SET_CAPTUREPOLARITY(&htim5, TIM_CHANNEL_3, TIM_INPUTCHANNELPOLARITY_RISING);
	__HAL_TIM_ENABLE_IT(&htim5, TIM_IT_CC3);

	HAL_GPIO_WritePin(ULTRASONIC_TRIG_GPIO_Port, ULTRASONIC_TRIG_Pin, GPIO_PIN_SET);
	delay_us(10); // Hardware blocking delay for 10us is perfectly safe
	HAL_GPIO_WritePin(ULTRASONIC_TRIG_GPIO_Port, ULTRASONIC_TRIG_Pin, GPIO_PIN_RESET);

	osDelay(60); // Wait for echo to process and settle
  }
  /* USER CODE END StartUltrasonicTask */
}

/**
  * @brief  Period elapsed callback in non blocking mode
  * @note   This function is called  when TIM7 interrupt took place, inside
  * HAL_TIM_IRQHandler(). It makes a direct call to HAL_IncTick() to increment
  * a global variable "uwTick" used as application time base.
  * @param  htim : TIM handle
  * @retval None
  */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  /* USER CODE BEGIN Callback 0 */

  /* USER CODE END Callback 0 */
  if (htim->Instance == TIM7) {
    HAL_IncTick();
  }
  /* USER CODE BEGIN Callback 1 */

  /* USER CODE END Callback 1 */
}

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}

#ifdef  USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */

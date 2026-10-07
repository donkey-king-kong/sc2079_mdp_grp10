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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "oled.h"
#include "ICM20948.h"
#include "robot_config.h"   // every tunable value (edit this during calibration)
#include "motion.h"         // straights, turns, heading
#include "field_tools.h"    // knob-and-button test modes, TD log
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
/* All tunable values live in robot_config.h. */
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
 ADC_HandleTypeDef hadc1;

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
  .stack_size = 512 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};
/* Definitions for communicateTask */
osThreadId_t communicateTaskHandle;
const osThreadAttr_t communicateTask_attributes = {
  .name = "communicateTask",
  .stack_size = 512 * 4,
  .priority = (osPriority_t) osPriorityAboveNormal,
};
/* Definitions for motorTask */
osThreadId_t motorTaskHandle;
const osThreadAttr_t motorTask_attributes = {
  .name = "motorTask",
  .stack_size = 512 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};
/* Definitions for oledTask */
osThreadId_t oledTaskHandle;
const osThreadAttr_t oledTask_attributes = {
  .name = "oledTask",
  .stack_size = 768 * 4,
  .priority = (osPriority_t) osPriorityLow,
};
/* Definitions for gyroTask */
osThreadId_t gyroTaskHandle;
const osThreadAttr_t gyroTask_attributes = {
  .name = "gyroTask",
  .stack_size = 512 * 4,
  .priority = (osPriority_t) osPriorityAboveNormal,
};
/* Definitions for ultrasonicTask */
osThreadId_t ultrasonicTaskHandle;
const osThreadAttr_t ultrasonicTask_attributes = {
  .name = "ultrasonicTask",
  .stack_size = 512 * 4,
  .priority = (osPriority_t) osPriorityBelowNormal,
};
/* Definitions for uartQueue */
osMessageQueueId_t uartQueueHandle;
const osMessageQueueAttr_t uartQueue_attributes = {
  .name = "uartQueue"
};
/* USER CODE BEGIN PV */

/* ==========================================================================
 *                     GLOBAL STATE
 *  Motion and heading state lives in motion.c, test-mode state in
 *  field_tools.c, and every tunable value in robot_config.h.
 * ========================================================================== */

/* --- Serial --- */
uint8_t rxByte;                                 // UART single-byte receive buffer
volatile uint8_t emergency_stop_requested = 0;  // Set by '!' on the UART; stops the current move

/* --- Encoders (accumulated by the motor task, read by motion.c) --- */
int32_t left_encoder_val = 0;                   // Total ticks since boot (Left)
int32_t right_encoder_val = 0;                  // Total ticks since boot (Right)

/* --- Gyroscope --- */
volatile float dash_gyroRate = 0.0f;            // Yaw rate in deg/s (CCW positive)

/* --- OLED display dashboard --- */
char oled_status_msg[32] = "Booting...";        // When not empty, shown instead of the dashboard
char dash_lastCmd[15] = "None";                 // "CMD:" line: last command, or test-mode info
float dash_ultraDist = 0.0;                     // Ultrasonic distance (cm)
int dash_encoderL = 0;                          // Left encoder total
int dash_encoderR = 0;                          // Right encoder total
uint16_t dash_direction = 0;                    // Encoder direction indicator
float dash_battV = 0.0f;                        // Battery voltage (smoothed)
int dash_battPct = 0;                           // Battery percentage (rough)

/* --- Ultrasonic sensor --- */
uint32_t tc1 = 0;                               // Timer capture 1 (Rising Edge)
uint32_t tc2 = 0;                               // Timer capture 2 (Falling Edge)
uint32_t echo = 0;                              // Delta time of echo pulse
uint8_t first_captured = 0;                     // State flag for input capture

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
static void MX_ADC1_Init(void);
void StartDefaultTask(void *argument);
void StartCommunicateTask(void *argument);
void StartMotorTask(void *argument);
void StartOledTask(void *argument);
void StartGyroTask(void *argument);
void StartUltrasonicTask(void *argument);

/* USER CODE BEGIN PFP */
static void bootMessage(const char *msg);

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
  MX_ADC1_Init();
  /* USER CODE BEGIN 2 */

  // 1. Initialize OLED and Show Boot Message
  OLED_Init();
  bootMessage("Booting...");

  // 2. Initialize IMU (Gyroscope)
  if (ICM20948_Init() == HAL_OK) {
	  bootMessage("IMU Found");
	  HAL_Delay(300);         // long enough to read; the RTOS isn't running yet
  } else {
	  bootMessage("IMU ERROR!");
	  while(1); // Halt execution if IMU fails (the message stays on screen)
  }

  // 3. Start UART Interrupt Listener
  HAL_UART_Receive_IT(&huart3, &rxByte, 1);
#if FIELD_TOOLS
  FieldTools_Init();        // PB1 knob as analog input
#endif

  // 4. Start Ultrasonic Timers
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
  * @brief ADC1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_ADC1_Init(void)
{

  /* USER CODE BEGIN ADC1_Init 0 */

  /* USER CODE END ADC1_Init 0 */

  ADC_ChannelConfTypeDef sConfig = {0};

  /* USER CODE BEGIN ADC1_Init 1 */

  /* USER CODE END ADC1_Init 1 */

  /** Configure the global features of the ADC (Clock, Resolution, Data Alignment and number of conversion)
  */
  hadc1.Instance = ADC1;
  hadc1.Init.ClockPrescaler = ADC_CLOCK_SYNC_PCLK_DIV4;
  hadc1.Init.Resolution = ADC_RESOLUTION_12B;
  hadc1.Init.ScanConvMode = DISABLE;
  hadc1.Init.ContinuousConvMode = DISABLE;
  hadc1.Init.DiscontinuousConvMode = DISABLE;
  hadc1.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
  hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;
  hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
  hadc1.Init.NbrOfConversion = 1;
  hadc1.Init.DMAContinuousRequests = DISABLE;
  hadc1.Init.EOCSelection = ADC_EOC_SINGLE_CONV;
  if (HAL_ADC_Init(&hadc1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure for the selected ADC regular channel its corresponding rank in the sequencer and its sample time.
  */
  sConfig.Channel = ADC_CHANNEL_8;
  sConfig.Rank = 1;
  sConfig.SamplingTime = ADC_SAMPLETIME_480CYCLES;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN ADC1_Init 2 */

  /* USER CODE END ADC1_Init 2 */

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
  htim12.Init.Prescaler = 16-1;
  htim12.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim12.Init.Period = 20000-1;
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
  sConfigOC.Pulse = 1500;
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

  /*Configure GPIO pin : PE0 */
  GPIO_InitStruct.Pin = GPIO_PIN_0;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOE, &GPIO_InitStruct);

}

/* USER CODE BEGIN 4 */

/* ==========================================================================
 *                     INTERRUPT CALLBACKS
 * ========================================================================== */

/**
 * @brief Serial Communication Interrupt Callback.
 *        Listens for incoming UART data and handles emergency stops immediately.
 */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
	if (huart -> Instance == USART3) {
		// Intercept emergency stop ('!') immediately so it bypasses queues
		if (rxByte == '!') {
			emergency_stop_requested = 1;
		} else {
			// Every other character goes to the command task. '!' is not queued:
			// it would otherwise end up at the front of the next command.
			osMessageQueuePut(uartQueueHandle, &rxByte, 0U, 0U);
		}

		// Re-arm the interrupt to listen for the next byte
		HAL_UART_Receive_IT(&huart3, &rxByte, 1);
	}
}

/**
 * @brief Microsecond delay using hardware timer (TIM6).
 *        Used to precisely trigger the ultrasonic sensor.
 */
void delay_us(uint16_t us)
{
	__HAL_TIM_SET_COUNTER(&htim6, 0);
	while(__HAL_TIM_GET_COUNTER(&htim6) < us);
}

/**
 * @brief Ultrasonic Input Capture Callback.
 *        Calculates distance based on the width of the echo pulse.
 */
void HAL_TIM_IC_CaptureCallback(TIM_HandleTypeDef *htim)
{
	if (htim->Instance == TIM5 && htim->Channel == HAL_TIM_ACTIVE_CHANNEL_3) {
		if (first_captured == 0) {
			// Rising edge detected: start timer capture
			tc1 = HAL_TIM_ReadCapturedValue(htim, TIM_CHANNEL_3);
			first_captured = 1;
			__HAL_TIM_SET_CAPTUREPOLARITY(htim, TIM_CHANNEL_3, TIM_INPUTCHANNELPOLARITY_FALLING);
		} else if (first_captured == 1) {
			// Falling edge detected: stop timer and calculate delta
			tc2 = HAL_TIM_ReadCapturedValue(htim, TIM_CHANNEL_3);
			__HAL_TIM_SET_COUNTER(htim, 0);

			// Handle timer overflow
			if (tc2 > tc1) {
				echo = tc2 - tc1;
			} else {
				echo = (65535 - tc1) + tc2;
			}

			// Convert raw timer ticks to centimeters
			dash_ultraDist = (float)echo * ULTRA_CM_PER_US + ULTRA_OFFSET_CM;   // calibrated: see robot_config.h

			// Reset state machine for the next reading
			first_captured = 0;
			__HAL_TIM_SET_CAPTUREPOLARITY(htim, TIM_CHANNEL_3, TIM_INPUTCHANNELPOLARITY_RISING);
			__HAL_TIM_DISABLE_IT(htim, TIM_IT_CC3);
		}
	}
}

/* Re-arms UART3 reception after a line error (noise, a cable being
 * plugged or unplugged). Without this the robot stops listening until
 * reset. If reception is still running, the call just returns busy. */
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART3) {
        HAL_UART_Receive_IT(&huart3, &rxByte, 1);
    }
}

/* ==========================================================================
 *                     BOOT MESSAGES
 * ========================================================================== */

/* Draws a status line straight to the OLED. Used during start-up, before the
 * OLED task is running (it normally does all the drawing), so messages like
 * "IMU ERROR!" are visible even if start-up stops there. */
static void bootMessage(const char *msg)
{
    strncpy(oled_status_msg, msg, sizeof(oled_status_msg) - 1);
    oled_status_msg[sizeof(oled_status_msg) - 1] = '\0';
    OLED_Clear();
    OLED_ShowString(0, 24, (uint8_t *)oled_status_msg);
    OLED_Refresh_Gram();
}

/* ==========================================================================
 *                     BATTERY
 * ========================================================================== */

/* Reads one ADC1 channel. Only ever called from the OLED task, so the
 * battery and the knob never fight over the ADC. Returns 0xFFFFFFFF on failure. */
uint32_t Board_ReadAdc(uint32_t channel)
{
    ADC_ChannelConfTypeDef c = {0};
    uint32_t raw = 0xFFFFFFFFu;
    c.Channel = channel;
    c.Rank = 1;
    c.SamplingTime = ADC_SAMPLETIME_480CYCLES;
    HAL_ADC_ConfigChannel(&hadc1, &c);
    HAL_ADC_Start(&hadc1);
    if (HAL_ADC_PollForConversion(&hadc1, 10) == HAL_OK) {
        raw = HAL_ADC_GetValue(&hadc1);
    }
    HAL_ADC_Stop(&hadc1);
    return raw;
}

/* Battery voltage from PB0 (11:1 divider: R27 100k / R26 10k). */
static float readBatteryVoltage(void)
{
    uint32_t raw = Board_ReadAdc(ADC_CHANNEL_8);
    if (raw == 0xFFFFFFFFu) {
        return dash_battV;              // keep last good reading
    }
    return ((float)raw * 3.3f / 4095.0f) * 11.0f;
}

/* Rough 3S Li-ion state of charge. Approximate: voltage sags under load. */
static int batteryPercent(float v)
{
    float cell = v / 3.0f;
    if (cell >= 4.15f) return 100;
    if (cell <= 3.30f) return 0;
    if (cell >  3.85f) return (int)(55.0f + (cell - 3.85f) * (45.0f / 0.30f));
    if (cell >  3.60f) return (int)(20.0f + (cell - 3.60f) * (35.0f / 0.25f));
    return (int)((cell - 3.30f) * (20.0f / 0.30f));
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
#if FIELD_TOOLS
  FieldTools_Run();                               // knob-and-button test modes; never returns
#else
  for(;;)
  {
      HAL_GPIO_TogglePin(LED3_GPIO_Port, LED3_Pin);   // heartbeat LED
      osDelay(100);
  }
#endif
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

  /* Commands from the RPi, one per line. Every reply starts with "A".
   *   SF<cm> / S<cm> / F<cm>   straight forward     -> "A d=<cm> e=<heading error>"
   *   SB<cm> / B<cm>           straight backward
   *   LF / LB / RF / RB <deg>  turn at full lock     -> "A a=<deg turned> e=<heading error>"
   *   ZH                       this direction is "north" (send once before the first move)
   *   D                        task done (shown on the OLED)
   *   TD / TV<us>              field tools (FIELD_TOOLS = 1 only)
   * Anything else              -> "A ERR <command>"
   * Moves stopped by '!' or the button end in " ABORT", timeouts in " TO". */
  char cmdBuffer[15];                             // command being received, e.g. "SF050"
  uint8_t receivedChar;
  uint8_t cmdIndex = 0;

  for(;;)
  {
	// Block until a character arrives from the UART interrupt
	if (osMessageQueueGet(uartQueueHandle, &receivedChar, NULL, osWaitForever) != osOK) {
		continue;
	}

	// Collect characters until the end of the line
	if (receivedChar != '\n' && receivedChar != '\r') {
		if (cmdIndex < 14) {
			cmdBuffer[cmdIndex] = receivedChar;
			cmdIndex++;
		}
		continue;
	}
	if (cmdIndex == 0) {
		continue;                                   // empty line (e.g. the \n after a \r)
	}
	cmdBuffer[cmdIndex] = '\0';
	cmdIndex = 0;

	// 1. PARSE: one or two letters then a number ("SF050", "LB090", "ZH", "S50")
	char c1 = ' ';
	char c2 = ' ';
	int value = 0;
	int parsed;
	if ((cmdBuffer[1] >= 'A' && cmdBuffer[1] <= 'Z') ||
		(cmdBuffer[1] >= 'a' && cmdBuffer[1] <= 'z')) {
		parsed = sscanf(cmdBuffer, "%c%c%d", &c1, &c2, &value);
	} else {
		parsed = sscanf(cmdBuffer, "%c%d", &c1, &value);
	}

	// 2. EXECUTE and prepare the reply text (without the leading "A")
	char replyText[64] = "";
	int isMove  = 0;                                 // reply with the move's result
	int unknown = 0;                                 // reply "A ERR <command>"

	if (parsed <= 0) {
		unknown = 1;
	} else {
		switch (c1) {
			case 'S':                                   // straight: SF / SB / S
				if (c2 == 'B') {
					Motion_Straight(-(float)value);
					isMove = 1;
				} else if (c2 == 'L' || c2 == 'R') {
					unknown = 1;                        // old slide commands: removed
				} else {
					Motion_Straight((float)value);
					isMove = 1;
				}
				break;

			case 'F':                                   // forward (alias)
				Motion_Straight((float)value);
				isMove = 1;
				break;

			case 'B':                                   // backward (alias)
				Motion_Straight(-(float)value);
				isMove = 1;
				break;

			case 'L':                                   // left turn: LF / LB / L
				Motion_Turn(1, (c2 == 'B') ? -1 : 1, (float)value);
				isMove = 1;
				break;

			case 'R':                                   // right turn: RF / RB / R
				Motion_Turn(0, (c2 == 'B') ? -1 : 1, (float)value);
				isMove = 1;
				break;

			case 'Z':                                   // ZH: zero heading
				if (c2 == 'H') {
					Motion_ZeroHeading();
					strcpy(replyText, "ZH");
				} else {
					unknown = 1;
				}
				break;

			case 'D':                                   // task done
				strcpy(oled_status_msg, "TASK A.5 DONE!");
				break;

#if FIELD_TOOLS
			case 'T':                                   // TD / TV
				if (!FieldTools_Command(c2, value, replyText, sizeof(replyText))) {
					unknown = 1;
				}
				break;
#endif

			default:
				unknown = 1;
				break;
		}
	}

	// 3. REPLY TO THE RPI
	char ackMsg[96];
	if (unknown) {
		snprintf(ackMsg, sizeof(ackMsg), "A ERR %s\n", cmdBuffer);
	} else if (isMove) {
		snprintf(ackMsg, sizeof(ackMsg), "A %s\n", Motion_Reply());
	} else if (replyText[0] != '\0') {
		snprintf(ackMsg, sizeof(ackMsg), "A %s\n", replyText);
	} else {
		snprintf(ackMsg, sizeof(ackMsg), "A\n");
	}
	HAL_UART_Transmit(&huart3, (uint8_t *)ackMsg, strlen(ackMsg), 100);

	// 4. DASHBOARD
	strcpy(dash_lastCmd, cmdBuffer);
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

  int16_t cnt_L = 0;
  int16_t cnt_R = 0;
  left_encoder_val = 0;
  right_encoder_val = 0;

  // Start the encoder, motor and servo timers
  HAL_TIM_Encoder_Start(&htim2, TIM_CHANNEL_ALL); // Motor A (Left) Encoder
  HAL_TIM_Encoder_Start(&htim3, TIM_CHANNEL_ALL); // Motor B (Right) Encoder

  HAL_TIM_PWM_Start(&htim4, TIM_CHANNEL_3);       // Motor A (Left) Fwd/Rev
  HAL_TIM_PWM_Start(&htim4, TIM_CHANNEL_4);
  HAL_TIM_PWM_Start(&htim9, TIM_CHANNEL_1);       // Motor B (Right) Fwd/Rev
  HAL_TIM_PWM_Start(&htim9, TIM_CHANNEL_2);
  HAL_TIM_PWM_Start(&htim12, TIM_CHANNEL_2);      // Steering Servo

  // Stationary with the wheels straight on boot
  __HAL_TIM_SET_COMPARE(&htim12, TIM_CHANNEL_2, SERVO_TRUE_CENTRE);
  __HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_3, 0);
  __HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_4, 0);
  __HAL_TIM_SET_COMPARE(&htim9, TIM_CHANNEL_1, 0);
  __HAL_TIM_SET_COMPARE(&htim9, TIM_CHANNEL_2, 0);
  osDelay(1000);

  // Clear startup garbage from encoders
  __HAL_TIM_SET_COUNTER(&htim2, 0);
  __HAL_TIM_SET_COUNTER(&htim3, 0);

  /* Infinite loop: 100 Hz */
  for(;;)
  {
	// 1. Encoder ticks since the last loop
	cnt_L = (int16_t)__HAL_TIM_GET_COUNTER(&htim2);
	cnt_R = (int16_t)__HAL_TIM_GET_COUNTER(&htim3);
	__HAL_TIM_SET_COUNTER(&htim2, 0);
	__HAL_TIM_SET_COUNTER(&htim3, 0);

	left_encoder_val += cnt_L;
	right_encoder_val += cnt_R;

	dash_encoderL = left_encoder_val;
	dash_encoderR = right_encoder_val;
	dash_direction = __HAL_TIM_IS_TIM_COUNTING_DOWN(&htim2);

	// 2. Drive the active move, or keep the motors off (motion.c)
	Motion_Step(cnt_L, cnt_R);

	osDelay(10);
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

  dash_battV = readBatteryVoltage();

  /* Infinite loop */
  for(;;)
  {
	OLED_Clear();

	// If an overarching system status exists (like boot up or test mode), show it exclusively
	if (strlen(oled_status_msg) > 0) {
		OLED_ShowString(0, 24, (uint8_t*)oled_status_msg);
	}
	// Otherwise, paint the main telemetry dashboard
	else {
		// Line 1: Last received RPi Command
		OLED_ShowString(0, 0, (uint8_t*) "CMD: ");
		OLED_ShowString(35, 0, (uint8_t*) dash_lastCmd);

		// Line 2: Gyroscope Angle & Ultrasonic Distance
		sprintf(textBuffer, "G: %.1f D: %.1fcm", headingDeg - headingZero, dash_ultraDist);   // heading relative to "north"
		OLED_ShowString(0, 12, (uint8_t*) textBuffer);

		// Line 3: Accumulated Left Encoder
		sprintf(textBuffer, "Enc L: %d", dash_encoderL);
		OLED_ShowString(0, 24, (uint8_t *) textBuffer);

		// Line 4: Accumulated Right Encoder
		sprintf(textBuffer, "Enc R: %d", dash_encoderR);
		OLED_ShowString(0, 36, (uint8_t *) textBuffer);

		// Line 5: Raw Direction Indicator & Battery Percentage
		dash_battV = dash_battV * 0.9f + readBatteryVoltage() * 0.1f;   // smooth out load sag
#if FIELD_TOOLS
		FieldTools_UpdateKnob();
#endif
		dash_battPct = batteryPercent(dash_battV);
		sprintf(textBuffer, "Dir:%d B:%d%%", dash_direction, dash_battPct);
		OLED_ShowString(0, 48, (uint8_t *) textBuffer);
	}

	OLED_Refresh_Gram();
	osDelay(100); // UI Refresh rate 10Hz
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
  double total_angle = 0.0;     // integrated heading (deg); other tasks read headingDeg
  double offset = 0.0;
  uint32_t tick = 0;
  int i = 0;

  // Give sensor hardware time to stabilize on power up
  strcpy(oled_status_msg, "Warmup IMU...");
  osDelay(2000);

  // ---------------------------------------------------------
  // START-UP CALIBRATION PHASE (Robots must be kept still!)
  // ---------------------------------------------------------
  strcpy(oled_status_msg, "Calibrating Gyro...");
  osDelay(500);

  // Read 100 samples to establish natural zero-drift offset
  while (i < 100) {
	  osDelay(30); // Get sample every 30ms
	  ICM_ReadData(&IMU_Data);
	  offset += (double)IMU_Data.z_gyro;
	  i++;
  }
  offset = offset / 100.0;

  strcpy(oled_status_msg, "Calib Done!");
  osDelay(500);
  strcpy(oled_status_msg, ""); // Clear status to show dashboard

  tick = HAL_GetTick();

  /* Infinite loop */
  for(;;)
  {
	// High-frequency 100Hz loop essential for accurate Euler integration
    osDelay(10);

	// 1. Fetch IMU data
	ICM_ReadData(&IMU_Data);

	// 2. Calculate true delta-time (dt) in seconds
	uint32_t current_tick = HAL_GetTick();
	double dt = (double)(current_tick - tick) / 1000.0;
	tick = current_tick;

	// 3. Subtract baseline drift offset from angular velocity
	double gz_raw = (double)IMU_Data.z_gyro;
	double gz_corrected;
	double dev = gz_raw - offset;

	if (robotStill && dev > -GYRO_STILL_BAND && dev < GYRO_STILL_BAND) {
		// Wheels still and the gyro reads close to zero: the robot really is
		// at rest. Track the zero-rate offset, which drifts as the sensor warms up.
		offset += 0.002 * dev;
		gz_corrected = 0.0;
	} else if (robotStill) {
		// Wheels still but the gyro reads a real rotation: the robot is being
		// turned by hand. Count it as rotation and leave the offset alone.
		gz_corrected = dev;
	} else {
		// No deadband: even slow drifts must be seen by the heading hold.
		gz_corrected = gz_raw - offset;
	}

	// 4. Integrate the filtered velocity to get absolute heading
	total_angle += gz_corrected * dt;
	dash_gyroRate = (float)gz_corrected;   // yaw rate, used by turns

	// 5. Publish for the other tasks (float: a single 32-bit write, never torn)
	headingDeg = (float)total_angle;
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
	// 1. Ensure trigger line is completely low before pulsing
	HAL_GPIO_WritePin(ULTRASONIC_TRIG_GPIO_Port, ULTRASONIC_TRIG_Pin, GPIO_PIN_RESET);
	delay_us(2);

	// 2. Prime the input capture interrupt to wait for the rising edge
	first_captured = 0;
	__HAL_TIM_SET_CAPTUREPOLARITY(&htim5, TIM_CHANNEL_3, TIM_INPUTCHANNELPOLARITY_RISING);
	__HAL_TIM_ENABLE_IT(&htim5, TIM_IT_CC3);

	// 3. Send 10us HIGH acoustic pulse
	HAL_GPIO_WritePin(ULTRASONIC_TRIG_GPIO_Port, ULTRASONIC_TRIG_Pin, GPIO_PIN_SET);
	delay_us(10); // Hardware blocking delay here is brief and safe
	HAL_GPIO_WritePin(ULTRASONIC_TRIG_GPIO_Port, ULTRASONIC_TRIG_Pin, GPIO_PIN_RESET);

	// 4. Yield task to allow echo interrupt processing
	osDelay(60);
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

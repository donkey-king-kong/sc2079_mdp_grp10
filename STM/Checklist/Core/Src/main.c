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
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

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
uint8_t sbuf[15] = "Hello World!\n\r";

/* --- SERIAL COMMUNICATION VARIABLES --- */
uint8_t rxByte;									// UART receive buffer

/* --- OLED DISPLAY VARIABLES --- */
char dash_lastCmd[15] = "None";					// RPi command
int dash_speedL = 0;							// Left motor speed
int dash_speedR = 0;							// Right motor speed
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
  OLED_ShowString(10, 5, "System Ready"); // show message on OLED display at line 10
  OLED_Refresh_Gram();

  // Initialize IMU
  ICM20948_Init();

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
  sConfig.IC1Polarity = TIM_ICPOLARITY_RISING;
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
  char debugBuf[100];

  /* Infinite loop */
  for(;;)
  {
	HAL_GPIO_TogglePin(LED3_GPIO_Port, LED3_Pin);
	int len = sprintf(debugBuf, "Time: %lu ms | EncL: %d | EncR: %d\r\n", diag_timer, dash_encoderL, dash_encoderR);
	HAL_UART_Transmit(&huart3, (uint8_t*)debugBuf, len, HAL_MAX_DELAY);
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

				// --- SEND ACK TO RPi ---
				uint8_t ackMsg[] = "A\n";
				HAL_UART_Transmit(&huart3, ackMsg, sizeof(ackMsg)-1, 100);

				// --- UPDATE OLED RPI COMMAND VARIABLE ---
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
	osDelay(1);
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
  uint16_t pwmVal_L = 0;
  uint16_t pwmVal_R = 0;

  // Encoder Variables
  int32_t cnt1_L = 0;
  int32_t cnt2_L = 0;
  int32_t cnt1_R = 0;
  int32_t cnt2_R = 0;
  int16_t diff_L = 0;
  int16_t diff_R = 0;
  //uint32_t diag_timer = 0;

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
  __HAL_TIM_SET_COMPARE(&htim12, TIM_CHANNEL_2, 150); // Center Servo (1.5ms pulse)
  __HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_3, 0);
  __HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_4, 0);
  __HAL_TIM_SET_COMPARE(&htim9, TIM_CHANNEL_1, 0);
  __HAL_TIM_SET_COMPARE(&htim9, TIM_CHANNEL_2, 0);

  __HAL_TIM_SET_COUNTER(&htim2, 0);
  __HAL_TIM_SET_COUNTER(&htim3, 0);

  /* Infinite loop */
  for(;;)
  {
//    if(HAL_GetTick()-tick > 1000L)
//    {
//    	cnt2 = __HAL_TIM_GET_COUNTER(&htim2);
//    	if (__HAL_TIM_IS_TIM_COUNTING_DOWN(&htim2)) {
//    		if(cnt2<cnt1)
//    			diff = cnt1 - cnt2;
//    		else
//    			diff = (65535 - cnt2) + cnt1;
//    	} else {
//    		if(cnt2 > cnt1)
//    			diff = cnt2 - cnt1;
//    		else
//    			diff = (65535 - cnt1) + cnt2;
//    	}
//
//    	dash_encoderL = diff;
//    	dash_direction = __HAL_TIM_IS_TIM_COUNTING_DOWN(&htim2);
//
//    	cnt1 = __HAL_TIM_GET_COUNTER(&htim2);
//    	tick = HAL_GetTick();
//    }

	// Step A: Read Encoders
	cnt2_L = __HAL_TIM_GET_COUNTER(&htim2);
	cnt2_R = __HAL_TIM_GET_COUNTER(&htim3);

	diff_L = (int16_t) cnt2_L;
	diff_R = (int16_t) cnt2_R;

	__HAL_TIM_SET_COUNTER(&htim2, 0);
	__HAL_TIM_SET_COUNTER(&htim3, 0);

	dash_encoderL = (int)diff_L;
	dash_encoderR = (int)diff_R;
	dash_direction = __HAL_TIM_IS_TIM_COUNTING_DOWN(&htim2);

	// Step B: Diagnostic Loop (Changes state every 2000ms)
	diag_timer += 10;

	if (diag_timer < 2000)
	{
		__HAL_TIM_SET_COMPARE(&htim12, TIM_CHANNEL_2, 100); //1.0ms pulse
	}
	else if (diag_timer < 4000)
	{
		__HAL_TIM_SET_COMPARE(&htim12, TIM_CHANNEL_2, 200); //2.0ms pulse
	}
	else if (diag_timer < 6000)
	{
		__HAL_TIM_SET_COMPARE(&htim12, TIM_CHANNEL_2, 150); //1.5ms pulse
	}
	else if (diag_timer < 8000)
	{
		// Drive forward
		__HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_3, 2500);
		__HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_4, 0);

		__HAL_TIM_SET_COMPARE(&htim9, TIM_CHANNEL_1, 2500);
		__HAL_TIM_SET_COMPARE(&htim9, TIM_CHANNEL_2, 0);
	}
	else if (diag_timer < 10000)
	{
		// Drive backward
		__HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_3, 0);
		__HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_4, 2500);

		__HAL_TIM_SET_COMPARE(&htim9, TIM_CHANNEL_1, 0);
		__HAL_TIM_SET_COMPARE(&htim9, TIM_CHANNEL_2, 2500);
	}
	else if (diag_timer < 12000)
	{
		// Drive forward
		__HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_3, 0);
		__HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_4, 0);

		__HAL_TIM_SET_COMPARE(&htim9, TIM_CHANNEL_1, 0);
		__HAL_TIM_SET_COMPARE(&htim9, TIM_CHANNEL_2, 0);
	}
	else
	{
		diag_timer = 0;
	}
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

  /* Infinite loop */
  for(;;)
  {
	OLED_Clear();

	// 1. RPi Instructions
	OLED_ShowString(0, 0, "CMD: ");
	OLED_ShowString(35, 0, (uint8_t*) dash_lastCmd);

	// 2. Motor Speed
	sprintf(textBuffer, "Spd L:%d R:%d", dash_speedL, dash_speedR);
	OLED_ShowString(0, 12, (uint8_t*) textBuffer);

	// 3. Gyroscope Angle
	sprintf(textBuffer, "G: %.1f D: %.1fcm", (float)dash_gyroZ, dash_ultraDist);
	OLED_ShowString(0, 24, (uint8_t*) textBuffer);

	sprintf(textBuffer, "Enc L: %d R: %d", dash_encoderL, dash_encoderR);
	OLED_ShowString(0, 36, (uint8_t *) textBuffer);

	sprintf(textBuffer, "Dir: %d", dash_direction);
	OLED_ShowString(0, 48, (uint8_t *) textBuffer);

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

  /* Infinite loop */
  for(;;)
  {
    // Fetch IMU data
	ICM_ReadData(&IMU_Data);

	// Update gyro dashboard variable
	dash_gyroZ = (int)IMU_Data.z_gyro;

	osDelay(100);
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

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
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* ==========================================================================
 *                     CALIBRATION CONTROL PANEL
 * ========================================================================== */

/* --- 1. DISTANCE & GYRO CALIBRATION --------------------------------------- */
#define TICKS_PER_CM            62.568f  // Tweak if robot travels more or less than 10cm
#define SLIDE_TICKS_PER_CM      75.19f  // Specific tuning multiplier used in slide maneuvers
#define BACKWARD_MULTIPLIER     1.12f   // Scaling factor for backward movement

/* --- 2. MOTOR BIAS (HARDWARE OFFSETS) ------------------------------------- */
// Move Straight
#define RIGHT_MOTOR_BIAS        1.55f   // Decrease if drifting left, increase if drifting right

// Left Turn (90 to 360 degrees)
#define TURN_BIAS_DEG_L         26.52f  // Increase if over-turning, decrease if under-turning
#define TURN_SCALE_L            0.9407f // Multiplier for large left turns

// Left Turn (<= 45 degrees)
#define TURN_BIAS_DEG_L_SMALL   5.0f    // Static bias for small left turns
#define TURN_SCALE_L_SMALL      0.95f   // Multiplier for small left turns

// Right Turn (90 to 360 degrees)
#define TURN_BIAS_DEG_R         20.83f  // Static bias for large right turns
#define TURN_SCALE_R            0.9813f // Multiplier for large right turns

// Right Turn (<= 45 degrees)
#define TURN_BIAS_DEG_R_SMALL   5.0f    // Proportional bias for small right turns
#define TURN_SCALE_R_SMALL      0.95f   // Multiplier for small right turns

// Misc Turn Biases
#define LEFT_TURN_POWER_SCALE	0.85f	// Dampen right motor during left turns
#define TURN_SLAVE_RATIO        0.59f   // Speed ratio of inner wheel during arc turns
#define TURN_STOP_TOLERANCE_DEG 1.0f    // Degrees of allowable error to consider turn finished

/* --- 3. SERVO CALIBRATION ------------------------------------------------- */
#define SERVOCENTER             150     // PWM value for centered steering
#define SERVOLEFT               100     // PWM value for max left steering
#define SERVORIGHT              200     // PWM value for max right steering
#define TURNLEFT_TH             115     // Threshold to consider servo in 'left' state
#define TURNRIGHT_TH            195     // Threshold to consider servo in 'right' state

/* --- 4. PID GAINS (Tune for different floors!) ---------------------------- */
// Straight Line Controller
#define PID_STRAIGHT_KP         1.8f    // Proportional: aggressiveness in seeking target
#define PID_STRAIGHT_KI         0.05f   // Integral: overcomes static friction / hills
#define GYRO_CORRECTION_KP      45.0f   // Proportional: corrects straight-line drift via gyro

// Turning Controller (Stepped PWM Output Based on Error)
#define PID_ANG_MAX             4800    // Cruise speed for large angle turns (> 45 deg)
#define PID_ANG_HIGH            4400    // Approaching speed (25 to 45 deg)
#define PID_ANG_MED             4100    // Deceleration zone speed (15 to 25 deg)
#define PID_ANG_LOW             3900    // Approach speed (6 to 15 deg)
#define PID_ANG_FINE            3800    // Final settle speed (2 to 6 deg)
#define PID_ANG_MIN_L           2900    // Minimum power to overcome turn friction (Left)
#define PID_ANG_MIN_R           3700    // Minimum power to overcome turn friction (Right)

/* --- 5. PWM POWER LIMITS (Max Power = 7199) ------------------------------- */
#define PID_STR_MAX             5000    // ~55% speed (leaves headroom for PID to adjust)
#define PID_STR_MIN             2800    // Minimum power to break static floor friction

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
/* Larger stacks prevent task-local buffers and formatting from exhausting the
 * original 1 KB allocations. */
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
 *                     GLOBAL STATE VARIABLES
 * ========================================================================== */

/* --- SERIAL COMMUNICATION --- */
uint8_t rxByte;                                 // UART single-byte receive buffer
int flag_done = 0;                              // General purpose completion flag
int magnitude = 0;                              // Parsed magnitude from UART command

/* --- MOTOR & MOVEMENT STATE --- */
uint16_t pwmVal_servo = SERVOCENTER;            // Current steering servo position
uint16_t pwmVal_R = 0;                          // Right motor PWM output
uint16_t pwmVal_L = 0;                          // Left motor PWM output
int times_acceptable = 0;                       // Counter to ensure robot settles at target
int e_brake = 0;                                // Flag to trigger immediate soft brake
int is_moving = 0;                              // Movement state (0 = Idle, 1 = Moving)
uint32_t move_start_time = 0;                   // Timestamp when current movement started
uint8_t move_finish_reason = 0;                 // 1 = Reached target successfully, 2 = Timeout
volatile uint8_t emergency_stop_requested = 0;  // Interrupt flag set by UART '!'
float distance_integral = 0.0;                  // Accumulated integral error for straight movement
uint32_t current_cmd_timeout = 5000;            // Dynamic timeout limit for current movement (ms)

/* --- ENCODER TARGETS & READINGS --- */
int32_t left_encoder_val = 0;                   // Total accumulated ticks (Left)
int32_t right_encoder_val = 0;                  // Total accumulated ticks (Right)
int32_t left_target = 0;                        // Target encoder ticks (Left)
int32_t right_target = 0;                       // Target encoder ticks (Right)
double target_angle = 0;                        // Target gyroscope heading (degrees)

/* --- GYROSCOPE READINGS --- */
double total_angle = 0;                         // Current integrated heading (degrees)
uint8_t gyroBuffer[20];                         // I2C data buffer for IMU
uint8_t ICMAddress = 0x68;                      // IMU I2C Address
double error_angle = 0;                         // Instantaneous heading error

/* --- OLED DISPLAY DASHBOARD --- */
char oled_status_msg[32] = "Booting...";        // System status text (Line 1)
char dash_lastCmd[15] = "None";                 // Last command received from RPi
double dash_gyroZ = 0.0;                        // Dashboard variable for heading
float dash_ultraDist = 0.0;                     // Dashboard variable for ultrasonic distance
int dash_encoderL = 0;                          // Dashboard variable for Left Encoder
int dash_encoderR = 0;                          // Dashboard variable for Right Encoder
uint16_t dash_direction = 0;                    // Encoder direction indicator
uint32_t diag_timer = 0;                        // Timer for diagnostic blinking
int16_t dash_speedL = 0;                        // Instantaneous speed (Left)
int16_t dash_speedR = 0;                        // Instantaneous speed (Right)

/* --- ULTRASONIC SENSOR --- */
uint32_t tc1 = 0;                               // Timer capture 1 (Rising Edge)
uint32_t tc2 = 0;                               // Timer capture 2 (Falling Edge)
uint32_t echo = 0;                              // Delta time of echo pulse
uint8_t first_captured = 0;                     // State flag for input capture
uint16_t distance = 0;                          // Calculated distance in cm
int k = 0;                                      // General purpose counter

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

  // 1. Initialize OLED and Show Boot Message
  OLED_Init();
  strcpy(oled_status_msg, "System Init OK");
  osDelay(500);

  // 2. Initialize IMU (Gyroscope)
  if (ICM20948_Init() == HAL_OK) {
	  strcpy(oled_status_msg, "IMU Found");
	  osDelay(500);
  } else {
	  strcpy(oled_status_msg, "IMU ERROR!");
	  while(1); // Halt execution if IMU fails
  }

  // 3. Start UART Interrupt Listener
  HAL_UART_Receive_IT(&huart3, &rxByte, 1);

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
			is_moving = 0;
		}

		// Pass normal characters to the FreeRTOS Queue for processing
		osMessageQueuePut(uartQueueHandle, &rxByte, 0U, 0U);

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
			dash_ultraDist = (echo * 0.0343 / 2) + 1;

			// Reset state machine for the next reading
			first_captured = 0;
			__HAL_TIM_SET_CAPTUREPOLARITY(htim, TIM_CHANNEL_3, TIM_INPUTCHANNELPOLARITY_RISING);
			__HAL_TIM_DISABLE_IT(htim, TIM_IT_CC3);
		}
	}
}

/* ==========================================================================
 *                     CUSTOM CONTROL FUNCTIONS
 * ========================================================================== */

/**
 * @brief  Stepped Proportional Controller for Turning Angles.
 * @param  errord: The error difference in degrees (target_angle - current_angle).
 * @retval PWM duty cycle magnitude to drive the motors.
 * @note   This function steps down speed as it approaches the target to prevent overshoot.
 */
int PID_Angle(double errord)
{
	int error = (int)(errord * 10); // Scale up for integer comparison
	error = abs(error);

	// Return PWM magnitude (stepped proportional control) based on distance to target
	if (error > 450) {                 // Large angle (> 45 deg): full cruise speed
		times_acceptable = 0;
		return PID_ANG_MAX;
	} else if (error > 250) {          // Approaching (< 45 deg): stepping down
		times_acceptable = 0;
		return PID_ANG_HIGH;
	} else if (error > 150) {          // Deceleration zone (< 25 deg)
		times_acceptable = 0;
		return PID_ANG_MED;
	} else if (error > 60) {           // Close to target (< 15 deg)
		times_acceptable = 0;
		return PID_ANG_LOW;
	} else if (error > 20) {           // Settle zone (< 6 deg)
		times_acceptable = 0;
		return PID_ANG_FINE;
	} else if (error >= 2) {           // Final inch (< 2 deg)
		times_acceptable++;
		// Apply minimum PWM specific to the turn direction to overcome friction
		if (errord > 0) {
			return PID_ANG_MIN_L;      // Left turn minimum
		} else {
			return PID_ANG_MIN_R;      // Right turn minimum
		}
	} else {                           // Target Reached
		times_acceptable++;
		return 0;
	}
}

/**
 * @brief  PI Controller for Straight Movements (Encoder-based).
 * @param  error: The difference in encoder ticks (target_ticks - current_ticks).
 * @retval PWM duty cycle magnitude to drive the motors.
 */
int PID_Control(int error)
{
    error = abs(error);

    // 1. Proportional Term (Drive strong when far away)
    float p_term = PID_STRAIGHT_KP * error;

    // 2. Integral Term (Accumulate ONLY near target to prevent windup/saturation)
    if (error < 1000) {
        distance_integral += error;
        if (distance_integral > 5000) distance_integral = 5000; // Anti-windup cap
    } else {
        distance_integral = 0.0;
    }
    float i_term = PID_STRAIGHT_KI * distance_integral;

    // 3. Calculate Total PWM Output
    int total_pwm = (int)(p_term + i_term);

    // 4. Clamp output to safe hardware limits
    if (total_pwm > PID_STR_MAX) total_pwm = PID_STR_MAX;
    if (total_pwm < PID_STR_MIN && error > 20) total_pwm = PID_STR_MIN;

    // 5. Check if we have arrived
    if (error <= 20) {
        times_acceptable++;
        return 0;
    } else {
        times_acceptable = 0;
        return total_pwm;
    }
}

/**
 * @brief  Checks if the robot has finished its current movement command.
 * @retval 0 if finished or timed out, 1 if still moving.
 * @note   Handles settling time (`times_acceptable`) and dynamic timeouts.
 */
int finishCheck()
{
	uint32_t elapsed_time = HAL_GetTick() - move_start_time;

    // The motor task has already requested braking (target crossed)
    if (!is_moving) {
        return 0;
    }

    // Success Condition: Error has been minimal for ~20 ticks (approx 200ms)
    if (times_acceptable > 20) {
    	is_moving = 0;
        move_finish_reason = 1;			// Success status
        e_brake = 1; 					// Signal motor task to cut PWM
        times_acceptable = 0;
        return 0; 						// Finished
    }

    // Fail-safe Condition: Dynamic timeout triggered to prevent stalling
    if (elapsed_time > current_cmd_timeout) {
        is_moving = 0;
        move_finish_reason = 2;			// Timeout status
        e_brake = 1;   					// Signal motor task to cut PWM
        times_acceptable = 0;
        return 0; 						// Force-stop moving
    }

    return 1; 							// Still moving
}

/**
 * @brief  Commands the robot to drive straight by a set distance.
 * @param  distance: Distance in cm (positive for forward, negative for backward).
 */
void moveCarStraight(double distance)
{
	// Convert target cm into hardware encoder ticks
	int tick_distance = (int)(distance * TICKS_PER_CM);

	// Center steering servo before moving
	pwmVal_servo = SERVOCENTER;
	osDelay(300);

	// Reset movement states
	e_brake = 0;
	times_acceptable = 0;
	move_finish_reason = 0;
	is_moving = 1;
	distance_integral = 0.0;
	move_start_time = HAL_GetTick();

	// Calculate a dynamic timeout (60ms per cm + 4000ms baseline safety buffer)
	current_cmd_timeout = (uint32_t)(fabs(distance) * 60.0) + 4000U;

	// Set a high baseline (75000) to safely prevent integer underflow during math
	left_encoder_val = 75000;
	right_encoder_val = 75000;
	left_target = 75000 + tick_distance;
	right_target = 75000 + tick_distance;

	// Block caller until target is reached, yielding CPU so MotorTask can run
	while (finishCheck()) {
		osDelay(10);
	}
}

/**
 * @brief  Commands the robot to perform a pivot right turn.
 * @param  angle: Angle in degrees to turn.
 */
void moveCarRight(double angle)
{
	double calibrated_angle;

	// Set steering servo fully right
    pwmVal_servo = SERVORIGHT;
    osDelay(300);

    // Reset movement states
    e_brake = 0;
    times_acceptable = 0;
    move_finish_reason = 0;
    is_moving = 1;
    distance_integral = 0.0;
    move_start_time = HAL_GetTick();

    // Apply linear calibration based on turn severity
    if (angle <= 50.0) {
        calibrated_angle = (angle * TURN_SCALE_R_SMALL) - TURN_BIAS_DEG_R_SMALL;
    } else {
        calibrated_angle = (angle * TURN_SCALE_R) - TURN_BIAS_DEG_R;
    }

    // Right turns decrease the gyro heading
    target_angle -= calibrated_angle;

    // Calculate dynamic timeout
    current_cmd_timeout = (uint32_t) (angle * 45.0) + 3500U;

    while (finishCheck()) {
        osDelay(10);
    }
}

/**
 * @brief  Commands the robot to perform a pivot left turn.
 * @param  angle: Angle in degrees to turn.
 */
void moveCarLeft(double angle)
{
	double calibrated_angle;

	// Set steering servo fully left
    pwmVal_servo = SERVOLEFT;
    osDelay(300);

    // Reset movement states
    e_brake = 0;
    times_acceptable = 0;
    move_finish_reason = 0;
    is_moving = 1;
    distance_integral = 0.0;
    move_start_time = HAL_GetTick();

    // Apply linear calibration based on turn severity
    if (angle <= 50.0) {
        calibrated_angle = (angle * TURN_SCALE_L_SMALL) - TURN_BIAS_DEG_L_SMALL;
    } else {
        calibrated_angle = (angle * TURN_SCALE_L) - TURN_BIAS_DEG_L;
    }

    // Left turns increase the gyro heading
    target_angle += calibrated_angle;

    // Calculate dynamic timeout
    current_cmd_timeout = (uint32_t) (angle * 45.0) + 3500U;

    while (finishCheck()) {
        osDelay(10);
    }
}

/**
 * @brief  Commands the robot to perform a sliding maneuver to the Right.
 * @param  forward: 1 for moving forward, -1 for moving backward.
 */
void moveCarSlideRight(int forward)
{
    int sign = (forward == 1) ? 1 : -1;
    e_brake = 0;
    times_acceptable = 0;

    // Step 1: Drive Straight to clear obstacle
    if (sign > 0) {
        moveCarStraight((450.0 / SLIDE_TICKS_PER_CM) * sign);
    } else {
        moveCarStraight((540.0 / SLIDE_TICKS_PER_CM) * sign);
    }
    while (finishCheck()) { osDelay(10); }
    osDelay(50); // Pause to stabilize physical inertia
    times_acceptable = 0;

    // Step 2: Turn Right
    moveCarRight(29.0 * sign);
    while (finishCheck()) { osDelay(10); }
    osDelay(50);
    times_acceptable = 0;

    // Step 3: Counter-steer Left to re-align heading
    moveCarLeft(29.0 * sign);
    while (finishCheck()) {  osDelay(10); }
    osDelay(50);
}

/**
 * @brief  Commands the robot to perform a sliding maneuver to the Left.
 * @param  forward: 1 for moving forward, -1 for moving backward.
 */
void moveCarSlideLeft(int forward)
{
    int sign = (forward == 1) ? 1 : -1;
    e_brake = 0;
    times_acceptable = 0;

    // Step 1: Drive Straight to clear obstacle
    if (sign > 0) {
        moveCarStraight((560.0 / SLIDE_TICKS_PER_CM) * sign);
    } else {
        moveCarStraight((700.0 / SLIDE_TICKS_PER_CM) * sign);
    }
    while (finishCheck()) { osDelay(10); }
	osDelay(50);
	times_acceptable = 0;

	// Step 2: Turn Left
	moveCarLeft(29.0 * sign);
	while (finishCheck()) { osDelay(10); }
	osDelay(50);
	times_acceptable = 0;

	// Step 3: Counter-steer Right to re-align heading
	moveCarRight(29.0 * sign);
	while (finishCheck()) { osDelay(10); }
	osDelay(50);
}

/**
 * @brief  Drives forward to a specified distance from an obstacle.
 * @param  target_dist_cm: The desired final stopping distance (e.g., 18.0f).
 * @note   This function first takes a stable ultrasonic reading, then commands a
 *         precise, encoder-based straight movement.
 */
void approachObstacle(float target_dist_cm)
{
    float stable_dist_reading = 0;
    int valid_readings = 0;

    // 1. Get a stable initial distance reading to avoid noise spikes.
    //    Take the average of 5 readings over 250ms.
    strcpy(oled_status_msg, "Scanning...");
    for (int i = 0; i < 5; i++) {
        // 'dash_ultraDist' is continuously updated by the ultrasonicTask
        if (dash_ultraDist < 250) { // Ignore readings that are clearly out of range
            stable_dist_reading += dash_ultraDist;
            valid_readings++;
        }
        osDelay(50);
    }

    // Check if we got any valid readings
    if (valid_readings > 0) {
        stable_dist_reading /= valid_readings;
    } else {
        strcpy(oled_status_msg, "Scan Failed!");
        osDelay(1000);
        return; // Exit if we can't see the obstacle
    }

    // 2. Calculate the distance to travel.
    float travel_distance = stable_dist_reading - target_dist_cm;

    // 3. Execute the precise, calibrated straight movement.
    if (travel_distance > 0) {
        char oled_buf[32];
        sprintf(oled_buf, "Moving: %.1fcm", travel_distance);
        strcpy(oled_status_msg, oled_buf);
        osDelay(500);

        moveCarStraight(travel_distance);
    }
    // If travel_distance is negative, we are already closer than the target, so do nothing.
}

/**
 * @brief  Executes one "petal" of the flower maneuver around an obstacle corner.
 * @param  clockwise: 1 for a standard (L90->R180) clockwise petal, 0 for counter-clockwise.
 * @note   This is a blocking function.
 */
void executeFlowerPetal(int clockwise)
{
    if (clockwise) {
        // Step 1: Swing Left arc out
        moveCarLeft(90.0);
        osDelay(150);

        // Step 2: Loop Right arc around the corner
        moveCarRight(165.0);
        osDelay(150);
    }
    else {
        // Counter-clockwise mirror for future use
    	//moveCarStraight(-20.0);
    	//osDelay(150);
    	// In communicateTask
    	total_angle = total_angle - target_angle;
    	target_angle = 0.0;
        moveCarRight(90.0);
        osDelay(150);
        total_angle = total_angle - target_angle;
        target_angle = 0.0;
        moveCarLeft(180.0);
        osDelay(150);
    	// In communicateTask
    	total_angle = total_angle - target_angle;
    	target_angle = 0.0;
        moveCarLeft(160.0);
        osDelay(150);
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
  char oled_buf[32];
  uint8_t test_step = 0; // Tracks which test is next (0 to 5)

  /* Infinite loop */
  for(;;)
  {
      // Wait for the user button press (PE0)
      if (HAL_GPIO_ReadPin(GPIOE, GPIO_PIN_0) == GPIO_PIN_RESET)
      {
          char test_name[10];
          int is_left = 0;
          double test_angle = 0.0;

          // Determine the parameters for the current test step
          switch(test_step) {
              case 0: strcpy(test_name, "R90");  is_left = 0; test_angle = 90.0;  break;
              case 1: strcpy(test_name, "R180"); is_left = 0; test_angle = 180.0; break;
              case 2: strcpy(test_name, "R360"); is_left = 0; test_angle = 360.0; break;
              case 3: strcpy(test_name, "L90");  is_left = 1; test_angle = 90.0;  break;
              case 4: strcpy(test_name, "L180"); is_left = 1; test_angle = 180.0; break;
              case 5: strcpy(test_name, "L360"); is_left = 1; test_angle = 360.0; break;
          }

          // 1. Alert user which test is armed
          sprintf(oled_buf, "Armed for %s", test_name);
          strcpy(oled_status_msg, oled_buf);

          // Wait for button release (Debounce)
          while(HAL_GPIO_ReadPin(GPIOE, GPIO_PIN_0) == GPIO_PIN_RESET) {
              osDelay(50);
          }
          osDelay(500);

          // 2. Start a countdown
          for (int count = 3; count > 0; count--)
          {
              sprintf(oled_buf, "%s Test in...%d", test_name, count);
              strcpy(oled_status_msg, oled_buf);
              osDelay(1000);
          }

          // 3. Clear status and run the turn command
          sprintf(oled_buf, "Executing %s...", test_name);
          strcpy(oled_status_msg, oled_buf);

          // Reset gyro heading to zero so the dashboard shows a clean test result
          total_angle = 0.0;
          target_angle = 0.0;

          // Block until the robot finishes turning
          if (is_left) {
              moveCarLeft(test_angle);
          } else {
              moveCarRight(test_angle);
          }

          // 4. Test is complete. Show a completion message.
          strcpy(oled_status_msg, "Done. Press btn.");

          // Wait for a button press to acknowledge and return to the dashboard
          while(HAL_GPIO_ReadPin(GPIOE, GPIO_PIN_0) != GPIO_PIN_RESET) {
              osDelay(100);
          }

          // 5. Advance the state machine for the next button press
          test_step++;
          if (test_step > 5) {
              test_step = 0; // Reset back to R90 after finishing L360
          }

          // --- CRITICAL CHANGE ---
          // Clear the status message to revert to the main dashboard
          strcpy(oled_status_msg, "");

          // Wait for button release before we can arm the next test
          while(HAL_GPIO_ReadPin(GPIOE, GPIO_PIN_0) == GPIO_PIN_RESET) {
              osDelay(50);
          }
      }

      // Blink diagnostic LED to show task is alive
      HAL_GPIO_TogglePin(LED3_GPIO_Port, LED3_Pin);
      osDelay(100);
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
	// Block task until a character arrives in the UART Queue
	if (osMessageQueueGet(uartQueueHandle, &receivedChar, NULL, osWaitForever) == osOK) {

		// Command strings from RPi end in a newline
		if (receivedChar == '\n' || receivedChar == '\r') {

			if (cmdIndex > 0) {
				// Command fully received; null-terminate it for parsing
				cmdBuffer[cmdIndex] = '\0';

				// 1. PARSE THE COMMAND
				char command_char1 = ' ';
				char command_char2 = ' ';
				int value = 0;
				int items_parsed = 0;

				// Check if the command is a 2-letter code (e.g., "SL50") or 1-letter ("F50")
				if ((cmdBuffer[1] >= 'A' && cmdBuffer[1] <= 'Z') ||
					(cmdBuffer[1] >= 'a' && cmdBuffer[1] <= 'z')) {
					items_parsed = sscanf(cmdBuffer, "%c%c%d", &command_char1, &command_char2, &value);
				} else {
					items_parsed = sscanf(cmdBuffer, "%c%d", &command_char1, &value);
				}

				// 2. EXECUTE THE MOVEMENT FUNCTION
				if (items_parsed > 0) {

				    // Reset Gyro baseline before launching any new maneuver
				    total_angle = 0.0;
				    target_angle = 0.0;

					switch (command_char1) {

						case 'S': 								// Straight or Slide
							if (command_char2 == 'L') {			// "SL": Slide Left
								moveCarSlideLeft(value);
							} else if (command_char2 == 'R') {	// "SR": Slide Right
								moveCarSlideRight(value);
							} else if (command_char2 == 'B') {
								// RPi sends "SB<val>" for backwards straight
								moveCarStraight(-value * BACKWARD_MULTIPLIER);
							} else {
								// "S<val>": Standard Straight
								moveCarStraight(value);
							}
							break;

						case 'F':								// Forward (Alias for Straight)
							moveCarStraight(value);
							break;

						case 'B':								// Backward (Alias for negative Straight)
							moveCarStraight(-value * BACKWARD_MULTIPLIER);
							break;

						case 'R':								// Right Turn
							moveCarRight(value);
							break;

						case 'L':								// Left Turn
							moveCarLeft(value);
							break;

						case 'O': // Using 'O' for Obstacle
							if (command_char2 == 'A') { // "OA": Obstacle Approach
								// The guide suggests 18.0cm, so we'll use that.
								approachObstacle(13.0f);
							}
							else if (command_char2 == 'P') { // "OP": Obstacle Petal
								// The guide shows a clockwise petal (1)
								executeFlowerPetal(0);
							}
							break;

						case 'D': // "DONE" confirmation from RPi
							strcpy(oled_status_msg, "TASK A.5 DONE!");
							// You need a function to stop motors completely. Let's create a placeholder.
							// motors_stop(); // We will add this simple helper.
							e_brake = 1; // For now, just trigger a brake.
							break;

						/* Can add more cases here */
					}
				}

				// 3. SEND ACKNOWLEDGEMENT TO RPI
				char ackMsg[64];
				if (command_char1 == 'L' || command_char1 == 'R') {
					// Turn ACKs include specific angle metrics for remote calibration scripts
					snprintf(ackMsg, sizeof(ackMsg), "A G:%.1f T:%.1f %s\n",
							(float)total_angle, (float)target_angle,
							move_finish_reason == 2 ? "TIMEOUT" : "TARGET");
				} else {
					// Standard ACK
					snprintf(ackMsg, sizeof(ackMsg), "A\n");
				}
				HAL_UART_Transmit(&huart3, (uint8_t *)ackMsg, strlen(ackMsg), 100);

				// 4. UPDATE DASHBOARD AND RESET
				strcpy(dash_lastCmd, cmdBuffer);
				cmdIndex = 0; // Reset index to build the next incoming command
			}
		} else {
			// Newline not yet received, append character safely
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

  // 1. Task Local Variables
  int16_t cnt_L = 0;
  int16_t cnt_R = 0;
  pwmVal_L = 0;
  pwmVal_R = 0;
  left_encoder_val = 0;
  right_encoder_val = 0;

  // 2. Start Hardware Timers
  HAL_TIM_Encoder_Start(&htim2, TIM_CHANNEL_ALL); // Motor A (Left) Encoder
  HAL_TIM_Encoder_Start(&htim3, TIM_CHANNEL_ALL); // Motor B (Right) Encoder

  HAL_TIM_PWM_Start(&htim4, TIM_CHANNEL_3);       // Motor A (Left) Fwd/Rev
  HAL_TIM_PWM_Start(&htim4, TIM_CHANNEL_4);
  HAL_TIM_PWM_Start(&htim9, TIM_CHANNEL_1);       // Motor B (Right) Fwd/Rev
  HAL_TIM_PWM_Start(&htim9, TIM_CHANNEL_2);
  HAL_TIM_PWM_Start(&htim12, TIM_CHANNEL_2);      // Steering Servo

  // 3. Force Robot Stationary and Centered on Boot
  __HAL_TIM_SET_COMPARE(&htim12, TIM_CHANNEL_2, SERVOCENTER);
  __HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_3, 0);
  __HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_4, 0);
  __HAL_TIM_SET_COMPARE(&htim9, TIM_CHANNEL_1, 0);
  __HAL_TIM_SET_COMPARE(&htim9, TIM_CHANNEL_2, 0);
  osDelay(1000);

  // Clear startup garbage from encoders
  __HAL_TIM_SET_COUNTER(&htim2, 0);
  __HAL_TIM_SET_COUNTER(&htim3, 0);

  /* Infinite loop */
  for(;;)
  {
	// ---------------------------------------------------------
	// STEP A: Read and Accumulate Encoders
	// ---------------------------------------------------------
	cnt_L = (int16_t)__HAL_TIM_GET_COUNTER(&htim2);
	cnt_R = (int16_t)__HAL_TIM_GET_COUNTER(&htim3);

	__HAL_TIM_SET_COUNTER(&htim2, 0);
	__HAL_TIM_SET_COUNTER(&htim3, 0);

	dash_speedL = cnt_L;
	dash_speedR = cnt_R;

	left_encoder_val += cnt_L;
	right_encoder_val += cnt_R;

	dash_encoderL = left_encoder_val;
	dash_encoderR = right_encoder_val;
	dash_direction = __HAL_TIM_IS_TIM_COUNTING_DOWN(&htim2);

	// ---------------------------------------------------------
	// STEP B: Handle Braking and Emergency Stops
	// ---------------------------------------------------------
	if (e_brake) {
		// Soft Brake Triggered
		pwmVal_L = 0;
		pwmVal_R = 0;
		left_target = left_encoder_val;
		right_target = right_encoder_val;

		// Cut PWM immediately to prevent H-Bridge damage from 100% active braking
		__HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_3, 0);
		__HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_4, 0);
		__HAL_TIM_SET_COMPARE(&htim9, TIM_CHANNEL_1, 0);
		__HAL_TIM_SET_COMPARE(&htim9, TIM_CHANNEL_2, 0);
		e_brake = 0;

	} else if (emergency_stop_requested) {
		// Hard UART Emergency Stop
		__HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_3, 0);
		__HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_4, 0);
		__HAL_TIM_SET_COMPARE(&htim9, TIM_CHANNEL_1, 0);
		__HAL_TIM_SET_COMPARE(&htim9, TIM_CHANNEL_2, 0);

		pwmVal_servo = SERVOCENTER;
		__HAL_TIM_SET_COMPARE(&htim12, TIM_CHANNEL_2, pwmVal_servo);

		e_brake = 0;
		emergency_stop_requested = 0;

	// ---------------------------------------------------------
	// STEP C: Active Movement Control
	// ---------------------------------------------------------
	} else if (is_moving) {
		// Move Steering Servo
		__HAL_TIM_SET_COMPARE(&htim12, TIM_CHANNEL_2, pwmVal_servo);

		// Calculate instantaneous heading error
		error_angle = target_angle - total_angle;

		// --- SUB-STEP C1: LEFT TURN LOGIC ---
		if (pwmVal_servo < TURNLEFT_TH) {

			// Coasting Stop Trigger
			if (error_angle <= TURN_STOP_TOLERANCE_DEG) {
				is_moving = 0;
				move_finish_reason = 1;
				e_brake = 1;
				pwmVal_servo = SERVOCENTER;
				continue;
			}

			// Calculate Differential Speeds
			uint16_t base_power = PID_Angle(error_angle);
			pwmVal_R = (uint16_t) (base_power * 1.2f * LEFT_TURN_POWER_SCALE);   // Right acts as Master
			pwmVal_L = pwmVal_R * TURN_SLAVE_RATIO;                 						 // Left acts as Slave

			// Apply PWM to bridge based on error direction
			if (error_angle > 0) {
				__HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_3, 0);        // L Fwd
				__HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_4, pwmVal_L);
				__HAL_TIM_SET_COMPARE(&htim9, TIM_CHANNEL_1, 0);        // R Fwd
				__HAL_TIM_SET_COMPARE(&htim9, TIM_CHANNEL_2, pwmVal_R);
			} else {
				__HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_3, 0);        // Coast Backward
				__HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_4, 0);
				__HAL_TIM_SET_COMPARE(&htim9, TIM_CHANNEL_1, 0);
				__HAL_TIM_SET_COMPARE(&htim9, TIM_CHANNEL_2, 0);
			}

		// --- SUB-STEP C2: RIGHT TURN LOGIC ---
		} else if (pwmVal_servo > TURNRIGHT_TH) {

			// Coasting Stop Trigger
			if (error_angle >= -TURN_STOP_TOLERANCE_DEG) {
				is_moving = 0;
				move_finish_reason = 1;
				e_brake = 1;
				pwmVal_servo = SERVOCENTER;
				continue;
			}

			// Calculate Differential Speeds
			pwmVal_L = PID_Angle(error_angle);                          // Left acts as Master
			pwmVal_R = pwmVal_L * TURN_SLAVE_RATIO;                     // Right acts as Slave

			// Apply PWM to bridge based on error direction
			if (error_angle < 0) {
				__HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_3, 0);        // L Fwd
				__HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_4, pwmVal_L);
				__HAL_TIM_SET_COMPARE(&htim9, TIM_CHANNEL_1, 0);        // R Fwd
				__HAL_TIM_SET_COMPARE(&htim9, TIM_CHANNEL_2, pwmVal_R);
			} else {
				__HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_3, 0);        // Coast Backward
				__HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_4, 0);
				__HAL_TIM_SET_COMPARE(&htim9, TIM_CHANNEL_1, 0);
				__HAL_TIM_SET_COMPARE(&htim9, TIM_CHANNEL_2, 0);
			}

		// --- SUB-STEP C3: STRAIGHT LOGIC WITH GYRO DRIFT ASSIST ---
		} else {

			// 1. Baseline Speeds from Encoders
			pwmVal_R = PID_Control(right_target - right_encoder_val) * RIGHT_MOTOR_BIAS;
			pwmVal_L = PID_Control(left_target - left_encoder_val);

			// 2. Active Gyro drift correction via differential rear motors
			int left_correction = (int)(total_angle * GYRO_CORRECTION_KP);
			int right_correction = (int)(-total_angle * GYRO_CORRECTION_KP);
			int is_forward_cmd = (right_target >= 75000);

			// 3. Apply PWM
			if (is_forward_cmd) {
				if ((right_target - right_encoder_val) > 0) {
					pwmVal_L += left_correction;
					pwmVal_R += right_correction;

					__HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_3, 0);
					__HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_4, pwmVal_L);
					__HAL_TIM_SET_COMPARE(&htim9, TIM_CHANNEL_1, 0);
					__HAL_TIM_SET_COMPARE(&htim9, TIM_CHANNEL_2, pwmVal_R);
				} else {
					__HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_3, 0);
					__HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_4, 0);
					__HAL_TIM_SET_COMPARE(&htim9, TIM_CHANNEL_1, 0);
					__HAL_TIM_SET_COMPARE(&htim9, TIM_CHANNEL_2, 0);
					times_acceptable = 100;
				}
			} else { // Backwards
				if ((right_target - right_encoder_val) < 0) {
					pwmVal_L -= left_correction;
					pwmVal_R -= right_correction;

					__HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_3, pwmVal_L);
					__HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_4, 0);
					__HAL_TIM_SET_COMPARE(&htim9, TIM_CHANNEL_1, pwmVal_R);
					__HAL_TIM_SET_COMPARE(&htim9, TIM_CHANNEL_2, 0);
				} else {
					__HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_3, 0);
					__HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_4, 0);
					__HAL_TIM_SET_COMPARE(&htim9, TIM_CHANNEL_1, 0);
					__HAL_TIM_SET_COMPARE(&htim9, TIM_CHANNEL_2, 0);
					times_acceptable = 100;
				}
			}

			// Keep front steering servo perfectly centered during straight runs
			pwmVal_servo = SERVOCENTER;
			__HAL_TIM_SET_COMPARE(&htim12, TIM_CHANNEL_2, pwmVal_servo);
		}

	// ---------------------------------------------------------
	// STEP D: Idle State Enforcement
	// ---------------------------------------------------------
	} else {
		// Force absolute zero to prevent whines, hums, and physical oscillations
		__HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_3, 0);
		__HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_4, 0);
		__HAL_TIM_SET_COMPARE(&htim9, TIM_CHANNEL_1, 0);
		__HAL_TIM_SET_COMPARE(&htim9, TIM_CHANNEL_2, 0);

		// ADDED BY CALUDE
		is_moving = 0;
		move_finish_reason = 1;
		e_brake = 1;

		pwmVal_L = 0;
		pwmVal_R = 0;
	}

	osDelay(10); // Loop execution frequency 100Hz

	// Prevent rollover on settling timer
	if (times_acceptable > 1000) {
		times_acceptable = 1001;
	}
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
		sprintf(textBuffer, "G: %.1f D: %.1fcm", (float)dash_gyroZ, dash_ultraDist);
		OLED_ShowString(0, 12, (uint8_t*) textBuffer);

		// Line 3: Accumulated Left Encoder
		sprintf(textBuffer, "Enc L: %d", dash_encoderL);
		OLED_ShowString(0, 24, (uint8_t *) textBuffer);

		// Line 4: Accumulated Right Encoder
		sprintf(textBuffer, "Enc R: %d", dash_encoderR);
		OLED_ShowString(0, 36, (uint8_t *) textBuffer);

		// Line 5: Raw Direction Indicator
		sprintf(textBuffer, "Dir: %d", dash_direction);
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
	double gz_corrected = (double)IMU_Data.z_gyro - offset;

	// Filter mechanical deadband noise (< 0.35 deg/sec) using floating math
	if (fabs(gz_corrected) < 0.35) {
		gz_corrected = 0.0;
	}

	// 4. Integrate the filtered velocity to get absolute heading
	total_angle += gz_corrected * dt;

	// 5. Publish to global dashboard variable
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

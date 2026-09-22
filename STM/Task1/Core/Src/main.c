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
#define TICKS_PER_CM            75.5f    // PHASE 3a: measured (was 62.568, 17% short on every floor). [CAL]
#define SLIDE_TICKS_PER_CM      75.19f  // Specific tuning multiplier used in slide maneuvers
#define LEGACY_TICKS_PER_CM     62.568f // Old value, kept ONLY for the old moveCarStraight (slides, petal) so their tuning is unchanged
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
#define SERVOCENTER             1500    // PWM value for centered steering
#define SERVOLEFT               1000    // PWM value for max left steering
#define SERVORIGHT              2000    // PWM value for max right steering
#define TURNLEFT_TH             1150    // Threshold to consider servo in 'left' state
#define TURNRIGHT_TH            1950    // Threshold to consider servo in 'right' state

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


/* ============================================================
 * PHASE 1: SERVO CHARACTERISATION TEST  (temporary - remove after Phase 1)
 * ============================================================ */
#define TRACK_CM            15.7f   // Rear track, wheel centre to wheel centre (cm)
#define DEG2RAD             0.0174533f
#define TEST_SPEED_TPC      12.0f   // Test speed in encoder ticks per 10 ms (~19 cm/s)
#define TEST_PWM_START      3000.0f // PWM both wheels start from before the speed loop adjusts
#define TEST_SPEED_KP       150.0f  // PWM per tick of (filtered) speed error
#define TEST_SPEED_KI       4.0f    // PWM added to the integral per tick of error, every 10 ms
#define TEST_PWM_FLOOR      2400.0f // Never drop below this while driving: stops roll-stop-roll
#define SERVO_APPROACH_US   40      // Every run approaches its servo value from this far below
#define TEST_PWM_LIMIT      6500.0f // Never command more than this during a test
#define TEST_ARC_DEG        90.0f   // Arc test stops after this much heading change
#define TEST_CENTRE_CM      100.0f  // Centre test stops after this distance
#define TEST_TIMEOUT_MS     12000   // Safety cut-off for a single test run
#define TEST_SERVO_MIN      800     // Refuse servo values outside this window
#define TEST_SERVO_MAX      2200
#define PWM_BRAKE           7200    // CCR above ARR holds a pin high; both pins high = AT8236 brake

#define PHASE1_FIELD_TOOL   1       // 1 = knob + button test tool replaces the old button harness
#define KNOB_US_MIN         900     // Knob fully one way  -> this servo value
#define KNOB_US_MAX         2200    // Knob fully other way -> this servo value
#define TEST_LOG_SIZE       40      // Results kept for the "TD" dump command
#define BTN_LONG_MS         800     // Hold the button this long to switch TA/TC


/* ---- Phase 1 results ---- */
#define SERVO_TRUE_CENTRE   1494    // Measured straight-ahead (approach from below)

/* ---- PHASE 2: MOTOR CHARACTERISATION TEST ---- */
#define MT_RAMP_STEP        5.0f    // PWM change per 10 ms loop (500 PWM per second)
#define MT_START_TICKS      10      // A wheel counts as moving after this many ticks
#define MT_STILL_LOOPS      8       // ...and as stopped after this many loops with no ticks
#define MT_RAMP_MAX         6500.0f // Give up looking for breakaway above this
#define MT_FLOOR_RAMP_FROM  1000.0f // Floor ramp starts here (nothing moves below it)
#define MT_SWEEP_FIRST      1000    // Stand sweep: first PWM level
#define MT_SWEEP_LAST       7000    //              last PWM level (ARR is 7199)
#define MT_SWEEP_STEP       500     //              step between levels
#define MT_SWEEP_SETTLE_MS  400     // Let the speed settle at each level...
#define MT_SWEEP_MEASURE_MS 300     // ...then average it over this long
#define MT_STAND_ARM_US     950     // Stand test only starts with the knob fully clockwise

#define MT_SETTLE           0       // Motor test phases
#define MT_RAMP_UP          1
#define MT_HOLD             2
#define MT_RAMP_DOWN        3
#define MT_SWEEP            4
#define MT_REST             5

/* ============================================================
 * PHASE 3a: NEW STRAIGHT-LINE MOTION  (SF / SB / S / F / B commands)
 * Tuning knobs for Phase 5 calibration are marked [CAL].
 * ============================================================ */
/* Motor model from Phase 2 (cm/s per PWM, measured with wheels in the air).
 * Rescaled x0.829 to real cm after TICKS_PER_CM was corrected 62.568 -> 75.5. */
#define GAIN_L_FWD          0.0226f
#define GAIN_R_FWD          0.0196f
#define GAIN_L_REV          0.0179f
#define GAIN_R_REV          0.0193f
#define MOTOR_FF_BASE       2100.0f // Floor "keep moving" PWM from Phase 2
#define MOTOR_KICK_PWM      3300.0f // Start burst: covers the worst floor start seen (3205)
#define KICK_AFTER_LOOPS    3       // Apply the burst after this many loops with no ticks
#define MOTOR_PWM_MAX       6500.0f // Stay below the non-linear top end
#define WHEEL_KP            120.0f  // PWM per tick/10ms of speed error
#define WHEEL_KI            3.0f    // PWM added per tick of error, every 10 ms
#define WHEEL_I_LIMIT       1500.0f // Integral term limit (PWM)
#define BRAKE_ASSIST_TICKS  5.0f    // While slowing down: car this much too fast (ticks/10ms, ~7 cm/s) -> brake both

/* Speed profile */
#define STRAIGHT_V_MAX      35.0f   // [CAL] Cruise speed, cm/s
#define STRAIGHT_V_MIN      12.0f   // Slowest speed used: above the stick-slip region
#define STRAIGHT_ACCEL      60.0f   // cm/s per second when speeding up
#define STRAIGHT_DECEL      50.0f   // cm/s per second when slowing down
#define STRAIGHT_APPROACH   2.0f    // Last few cm are driven at STRAIGHT_V_MIN
#define STOP_T              0.050f  // [CAL] Stopping distance = STOP_T x speed (cm, speed in cm/s):
                                    //       the robot rolls ~50 ms after deciding to stop.
                                    //       Fitted from office-floor runs at 13-21 cm/s (0.039-0.055).

/* Heading hold: the servo steers gently to keep the start heading */
#define HEADING_KP_US       11.0f   // Servo us per degree of heading error
#define HEADING_MAX_US      60.0f   // Largest correction allowed
#define SERVO_RATE_US       4.0f    // Max servo change per 10 ms (keeps it smooth)
#define HEADING_KI_US       0.03f   // Centre learning: us per degree of error, per 10 ms
#define SERVO_TRIM_MAX      30.0f   // Learned centre may move at most this far from SERVO_TRUE_CENTRE

#define MV_SETTLE           0       // Straight-move phases
#define MV_DRIVE            1
#define MV_BRAKE            2
#define ST_SETTLE           0       // Test phases
#define ST_DRIVE            1
#define ST_BRAKE            2

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
float dash_battV = 0.0f;
int dash_battPct = 0;

/* --- ULTRASONIC SENSOR --- */
uint32_t tc1 = 0;                               // Timer capture 1 (Rising Edge)
uint32_t tc2 = 0;                               // Timer capture 2 (Falling Edge)
uint32_t echo = 0;                              // Delta time of echo pulse
uint8_t first_captured = 0;                     // State flag for input capture
uint16_t distance = 0;                          // Calculated distance in cm
int k = 0;                                      // General purpose counter


/* PHASE 1: servo test state. The communicate task starts a run,
 * the motor task executes it, so encoder data never crosses tasks. */
typedef struct {
    volatile uint8_t active;      // 1 while a test run owns the motors
    char     mode;                // 'A' = arc, 'C' = centre
    uint16_t servo_us;            // Servo pulse width under test (us)
    uint8_t  phase;               // ST_SETTLE / ST_DRIVE / ST_BRAKE
    uint32_t phaseTick;           // HAL tick when the current phase began
    int32_t  startL, startR;      // Encoder totals at the start of driving
    double   startHeading;        // Gyro heading at the start of driving
    float    pwmL, pwmR;          // Speed-loop integral terms
    float    fL, fR;              // Filtered wheel speeds (ticks per 10 ms)
    uint8_t  timedOut;            // 1 if the run hit TEST_TIMEOUT_MS
    char     result[48];          // Full result line, sent in the ACK
    char     oledShort[15];       // Short result for the OLED CMD line
} ServoTest_t;

/* PHASE 2: motor test state. Same split as the servo test: started by
 * runMotorTest(), executed 10 ms at a time inside the motor task. */
typedef struct {
    volatile uint8_t active;      // 1 while a motor test owns the motors
    char     kind;                // 'F' = floor, 'S' = stand (wheels in the air)
    int8_t   dir;                 // +1 forward, -1 reverse
    uint8_t  phase;               // MT_SETTLE ... MT_REST
    uint32_t phaseTick;           // HAL tick when the current phase began
    float    pwm;                 // PWM being applied to both wheels
    int32_t  cumL, cumR;          // Ticks since the ramp started (magnitude)
    uint16_t goL, goR;            // PWM at which each wheel started moving (0 = never)
    uint16_t stopL, stopR;        // PWM at which each wheel stopped on the way down
    uint8_t  stillL, stillR;      // Consecutive loops with no ticks
    uint16_t level;               // Current sweep PWM level
    int32_t  sumL, sumR;          // Ticks accumulated while measuring a sweep level
    uint32_t measStart;           // HAL tick when measuring began
    uint16_t goCar[2];            // Floor: car breakaway PWM, [0] forward, [1] reverse
    uint8_t  aborted;             // 1 if the button or '!' stopped the test
} MotorTest_t;

MotorTest_t mt = {0};
volatile uint8_t testAbort = 0;                    // Set by a button press during any test

/* PHASE 3a: per-wheel speed controller state */
typedef struct {
    float   integ;                // Integral term (PWM)
    float   filt;                 // Filtered speed, ticks per 10 ms
    uint8_t still;                // Loops in a row with no ticks
} WheelCtl_t;

/* PHASE 3a: straight move. Started by runStraight(), executed in the motor task. */
typedef struct {
    volatile uint8_t active;      // 1 while a straight move owns the motors
    uint8_t  phase;               // MV_SETTLE / MV_DRIVE / MV_BRAKE
    uint32_t phaseTick;
    int8_t   dir;                 // +1 forward, -1 backward
    float    target;              // Distance to travel, cm (always positive)
    float    v;                   // Current profile speed, cm/s
    int32_t  startL, startR;      // Encoder totals when driving began
    double   heading0;            // Heading to hold (deg)
    float    servoCmd;            // Current servo command (us)
    uint16_t settleA, settleB;    // Servo settle times (ms): below-centre, then centre
    uint32_t deadline;            // Timeout tick
    uint8_t  timedOut;
    uint8_t  aborted;
    uint8_t  stillLoops;          // Used while waiting to stop after braking
    WheelCtl_t wl, wr;
    float    resDist, resHead;    // Results: distance travelled, heading change
    float    vBrake;              // Measured speed when braking started (cm/s)
    char     result[40];
} Move_t;

Move_t mv = {0};
float servoTrim = 0.0f;                            // Learned centre correction (us), kept between moves
volatile uint8_t robotStill = 0;                   // 1 when the wheels haven't moved for 0.5 s

ServoTest_t st = {0};
volatile float dash_gyroRate = 0.0f;               // Yaw rate in deg/s (CCW positive)
volatile uint16_t dash_knobUs = 1500;              // Servo us for the field tool (knob or "TV" command)
volatile int32_t  dash_knobRaw = -1;               // Raw knob ADC reading, -1 = read failed
char     st_log[TEST_LOG_SIZE][48];                // Ring buffer of test results
uint8_t  st_logHead  = 0;                          // Next slot to write
uint8_t  st_logCount = 0;                          // Number of valid entries

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
static void knobPinInit(void);   // PHASE 1: defined in USER CODE 4

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
  knobPinInit();   // PHASE 1: PB1 potentiometer as analog input

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
	int tick_distance = (int)(distance * LEGACY_TICKS_PER_CM);   // PHASE 3a: legacy scale

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

/* Reads the 11:1 divider on PB0 (R27/R26) and returns pack volts. */
static uint32_t adcReadChannel(uint32_t channel);   // PHASE 1: defined further down

static float readBatteryVoltage(void)
{
    uint32_t raw = adcReadChannel(ADC_CHANNEL_8);      // PHASE 1: shared ADC helper
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


/* Re-arms UART3 reception after a line error (noise, a cable being
 * plugged or unplugged). Without this the robot stops listening until
 * reset. If reception is still running, the call just returns busy. */
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART3) {
        HAL_UART_Receive_IT(&huart3, &rxByte, 1);
    }
}

/* ============================================================
 * PHASE 1: SERVO CHARACTERISATION TEST
 * ============================================================ */

/* Writes all four motor-driver compare registers in one place. */
static void setMotorPwm(uint16_t l3, uint16_t l4, uint16_t r1, uint16_t r2)
{
    __HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_3, l3);
    __HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_4, l4);
    __HAL_TIM_SET_COMPARE(&htim9, TIM_CHANNEL_1, r1);
    __HAL_TIM_SET_COMPARE(&htim9, TIM_CHANNEL_2, r2);
}

static float clampf(float v, float lo, float hi)
{
    return (v < lo) ? lo : (v > hi) ? hi : v;
}

/* Appends one line to the result log that "TD" prints. */
static void logResult(const char *line)
{
    strncpy(st_log[st_logHead], line, sizeof(st_log[0]) - 1);
    st_log[st_logHead][sizeof(st_log[0]) - 1] = '\0';
    st_logHead = (uint8_t)((st_logHead + 1) % TEST_LOG_SIZE);
    if (st_logCount < TEST_LOG_SIZE) st_logCount++;
}

/* One 10 ms step of a test run. Called ONLY from the motor task.
 *
 * Each rear wheel runs its own speed loop. The targets follow the
 * yaw rate the gyro measures, so the rear wheels roll along with
 * whatever the steering does instead of fighting it. That keeps
 * wheel slip out of the measurement. */
static void servoTestStep(int16_t cntL, int16_t cntR)
{
    uint32_t now = HAL_GetTick();

    // Servo gears have a little slack, so where they stop depends on which
    // side they came from. Always arrive from below so runs are comparable.
    if (st.phase == ST_SETTLE && (now - st.phaseTick) < 300) {
        __HAL_TIM_SET_COMPARE(&htim12, TIM_CHANNEL_2, st.servo_us - SERVO_APPROACH_US);
    } else {
        __HAL_TIM_SET_COMPARE(&htim12, TIM_CHANNEL_2, st.servo_us);
    }

    switch (st.phase) {

    case ST_SETTLE:                                    // motors off, let the servo arrive
        setMotorPwm(0, 0, 0, 0);
        if (now - st.phaseTick >= 900) {
            st.startL = left_encoder_val;
            st.startR = right_encoder_val;
            st.startHeading = total_angle;
            st.pwmL = TEST_PWM_START;
            st.pwmR = TEST_PWM_START;
            st.fL = 0.0f;
            st.fR = 0.0f;
            st.phase = ST_DRIVE;
            st.phaseTick = now;
        }
        break;

    case ST_DRIVE: {
        float dL = (float)(left_encoder_val  - st.startL) / TICKS_PER_CM;
        float dR = (float)(right_encoder_val - st.startR) / TICKS_PER_CM;
        float dist   = 0.5f * (dL + dR);
        float turned = (float)(total_angle - st.startHeading);
        int reached  = (st.mode == 'A') ? (fabsf(turned) >= TEST_ARC_DEG)
                                        : (dist >= TEST_CENTRE_CM);

        if (reached || emergency_stop_requested || testAbort ||
            (now - st.phaseTick) > TEST_TIMEOUT_MS) {
            st.timedOut = !reached;
            st.phase = ST_BRAKE;
            st.phaseTick = now;
            break;
        }

        // Rear-axle kinematics: outer wheel = v + w*T/2, inner = v - w*T/2
        float yawTicks = dash_gyroRate * DEG2RAD * (TRACK_CM * 0.5f)
                         * TICKS_PER_CM / 100.0f;      // per 10 ms
        float tgtL = clampf(TEST_SPEED_TPC - yawTicks, 0.0f, 3.0f * TEST_SPEED_TPC);
        float tgtR = clampf(TEST_SPEED_TPC + yawTicks, 0.0f, 3.0f * TEST_SPEED_TPC);

        st.fL = 0.6f * st.fL + 0.4f * (float)cntL;       // smooth the 1-tick jitter
        st.fR = 0.6f * st.fR + 0.4f * (float)cntR;
        float eL = tgtL - st.fL;
        float eR = tgtR - st.fR;

        st.pwmL = clampf(st.pwmL + TEST_SPEED_KI * eL, TEST_PWM_FLOOR, TEST_PWM_LIMIT);
        st.pwmR = clampf(st.pwmR + TEST_SPEED_KI * eR, TEST_PWM_FLOOR, TEST_PWM_LIMIT);
        float outL = clampf(st.pwmL + TEST_SPEED_KP * eL, TEST_PWM_FLOOR, TEST_PWM_LIMIT);
        float outR = clampf(st.pwmR + TEST_SPEED_KP * eR, TEST_PWM_FLOOR, TEST_PWM_LIMIT);

        setMotorPwm(0, (uint16_t)outL, 0, (uint16_t)outR);         // both forward
        break;
    }

    case ST_BRAKE:
    default:
        if (now - st.phaseTick < 300) {                // active brake
            setMotorPwm(PWM_BRAKE, PWM_BRAKE, PWM_BRAKE, PWM_BRAKE);
            break;
        }
        setMotorPwm(0, 0, 0, 0);
        if (now - st.phaseTick < 700) break;           // let the car fully stop

        {
            float dL = (float)(left_encoder_val  - st.startL) / TICKS_PER_CM;
            float dR = (float)(right_encoder_val - st.startR) / TICKS_PER_CM;
            float dist   = 0.5f * (dL + dR);
            float turned = (float)(total_angle - st.startHeading);
            const char *to = st.timedOut ? " TO" : "";

            if (st.mode == 'A') {
                float rad = fabsf(turned) * DEG2RAD;
                float R   = (rad > 0.01f) ? dist / rad : 0.0f;
                float outer = fmaxf(dL, dR);
                float inner = fminf(dL, dR);
                float io    = (outer > 0.1f) ? inner / outer : 0.0f;
                snprintf(st.result, sizeof(st.result),
                         "TA%u R=%.1f th=%.1f d=%.1f io=%.2f%s",
                         st.servo_us, R, turned, dist, io, to);
                snprintf(st.oledShort, sizeof(st.oledShort), "%u R%.1f", st.servo_us, R);
            } else {
                float perM = (dist > 1.0f) ? turned / (dist / 100.0f) : 0.0f;
                snprintf(st.result, sizeof(st.result),
                         "TC%u drift=%+.2f/m th=%.1f d=%.1f%s",
                         st.servo_us, perM, turned, dist, to);
                snprintf(st.oledShort, sizeof(st.oledShort), "%u %+.1f/m", st.servo_us, perM);
            }
        }
        __HAL_TIM_SET_COMPARE(&htim12, TIM_CHANNEL_2, SERVOCENTER);
        st.active = 0;
        break;
    }
}

/* Starts a test and blocks until it finishes. Called from the communicate task.
 *   TS<us>  hold the servo at <us>, no driving (for the mechanical-limit check)
 *   TC<us>  centre test: drive TEST_CENTRE_CM straight-ish, report heading drift
 *   TA<us>  arc test: drive until the heading changes TEST_ARC_DEG, report radius */
static void runServoTest(char mode, int servo_us)
{
    if (st.active) {
        snprintf(st.result, sizeof(st.result), "T ERR busy");
        return;
    }
    if (mode == 'D') {                                 // TD: dump the result log over UART
        char line[64];
        uint8_t first = (uint8_t)((st_logHead + TEST_LOG_SIZE - st_logCount) % TEST_LOG_SIZE);
        for (uint8_t i = 0; i < st_logCount; i++) {
            snprintf(line, sizeof(line), "%2u %s\r\n", i + 1,
                     st_log[(first + i) % TEST_LOG_SIZE]);
            HAL_UART_Transmit(&huart3, (uint8_t *)line, strlen(line), 100);
        }
        snprintf(st.result, sizeof(st.result), "TD %u results", st_logCount);
        snprintf(st.oledShort, sizeof(st.oledShort), "TD %u", st_logCount);
        return;
    }
    if (servo_us < TEST_SERVO_MIN || servo_us > TEST_SERVO_MAX) {
        snprintf(st.result, sizeof(st.result), "T ERR %d out of range", servo_us);
        snprintf(st.oledShort, sizeof(st.oledShort), "T ERR range");
        return;
    }
    if (mode == 'V') {                                 // TV: set the field tool value
        dash_knobUs = (uint16_t)servo_us;
        snprintf(st.result, sizeof(st.result), "TV%d set", servo_us);
        snprintf(st.oledShort, sizeof(st.oledShort), "TV%d", servo_us);
        return;
    }
    if (mode == 'S') {
        __HAL_TIM_SET_COMPARE(&htim12, TIM_CHANNEL_2, servo_us);
        snprintf(st.result, sizeof(st.result), "TS%d holding", servo_us);
        snprintf(st.oledShort, sizeof(st.oledShort), "TS%d", servo_us);
        return;
    }
    if (mode != 'A' && mode != 'C') {
        snprintf(st.result, sizeof(st.result), "T ERR use TS/TC/TA");
        snprintf(st.oledShort, sizeof(st.oledShort), "T ERR mode");
        return;
    }

    st.mode      = mode;
    st.servo_us  = (uint16_t)servo_us;
    st.timedOut  = 0;
    st.phase     = ST_SETTLE;
    st.phaseTick = HAL_GetTick();
    testAbort    = 0;
    st.active    = 1;                                  // motor task takes over from here

    while (st.active) {
        if (HAL_GPIO_ReadPin(GPIOE, GPIO_PIN_0) == GPIO_PIN_RESET) {
            testAbort = 1;                             // button pressed mid-run: stop
        }
        osDelay(20);
    }

    logResult(st.result);                              // keep it for "TD"
}

/* ============================================================
 * PHASE 2: MOTOR CHARACTERISATION TEST
 * ============================================================ */

/* Both wheels at the given PWMs, forward (dir > 0) or reverse. */
static void motorDrive(int dir, uint16_t l, uint16_t r)
{
    if (dir > 0) setMotorPwm(0, l, 0, r);
    else         setMotorPwm(l, 0, r, 0);
}

/* One 10 ms step of a motor test. Called ONLY from the motor task.
 *
 * For each direction:
 *   1. ramp both wheels up slowly and note the PWM where each starts turning,
 *   2. ramp back down slowly and note where each stops,
 *   3. stand test only: step through fixed PWM levels and measure each
 *      wheel's steady speed.
 * Every result goes into the "TD" log. */
static void motorTestStep(int16_t cntL, int16_t cntR)
{
    uint32_t now = HAL_GetTick();
    uint32_t el  = now - mt.phaseTick;
    int32_t  aL  = abs((int)cntL);
    int32_t  aR  = abs((int)cntR);
    char     line[48];

    if (testAbort || emergency_stop_requested) {
        setMotorPwm(0, 0, 0, 0);
        mt.aborted = 1;
        mt.active  = 0;
        return;
    }

    switch (mt.phase) {

    case MT_SETTLE:                                    // wheels straight, motors off
        setMotorPwm(0, 0, 0, 0);
        __HAL_TIM_SET_COMPARE(&htim12, TIM_CHANNEL_2, (el < 300)
                              ? (SERVO_TRUE_CENTRE - SERVO_APPROACH_US) : SERVO_TRUE_CENTRE);
        if (el >= 900) {
            mt.cumL = mt.cumR = 0;
            mt.goL = mt.goR = mt.stopL = mt.stopR = 0;
            mt.stillL = mt.stillR = 0;
            mt.pwm = (mt.kind == 'F') ? MT_FLOOR_RAMP_FROM : 0.0f;
            mt.phase = MT_RAMP_UP;
            mt.phaseTick = now;
        }
        break;

    case MT_RAMP_UP:
        mt.cumL += aL;
        mt.cumR += aR;
        if (!mt.goL && mt.cumL >= MT_START_TICKS) mt.goL = (uint16_t)mt.pwm;
        if (!mt.goR && mt.cumR >= MT_START_TICKS) mt.goR = (uint16_t)mt.pwm;

        if ((mt.goL && mt.goR) || mt.pwm >= MT_RAMP_MAX) {
            mt.phase = MT_HOLD;
            mt.phaseTick = now;
        } else {
            mt.pwm += MT_RAMP_STEP;
        }
        motorDrive(mt.dir, (uint16_t)mt.pwm, (uint16_t)mt.pwm);
        break;

    case MT_HOLD:                                      // make sure it is really rolling
        motorDrive(mt.dir, (uint16_t)mt.pwm, (uint16_t)mt.pwm);
        if (el >= 300) {
            mt.phase = MT_RAMP_DOWN;
            mt.phaseTick = now;
        }
        break;

    case MT_RAMP_DOWN:
        mt.stillL = (aL == 0) ? (uint8_t)(mt.stillL + 1) : 0;
        mt.stillR = (aR == 0) ? (uint8_t)(mt.stillR + 1) : 0;
        // The wheel really stopped MT_STILL_LOOPS loops ago, when PWM was higher
        if (!mt.stopL && mt.stillL >= MT_STILL_LOOPS)
            mt.stopL = (uint16_t)(mt.pwm + MT_STILL_LOOPS * MT_RAMP_STEP);
        if (!mt.stopR && mt.stillR >= MT_STILL_LOOPS)
            mt.stopR = (uint16_t)(mt.pwm + MT_STILL_LOOPS * MT_RAMP_STEP);

        if ((mt.stopL && mt.stopR) || mt.pwm <= MT_RAMP_STEP) {
            snprintf(line, sizeof(line), "%c%c go L=%u R=%u stop L=%u R=%u",
                     mt.kind, (mt.dir > 0) ? '+' : '-',
                     mt.goL, mt.goR, mt.stopL, mt.stopR);
            logResult(line);
            mt.goCar[(mt.dir > 0) ? 0 : 1] = (mt.goL > mt.goR) ? mt.goL : mt.goR;

            if (mt.kind == 'S') {
                mt.level = MT_SWEEP_FIRST;
                mt.sumL = mt.sumR = 0;
                mt.phase = MT_SWEEP;
            } else {
                mt.phase = MT_REST;
            }
            mt.phaseTick = now;
            setMotorPwm(0, 0, 0, 0);
            break;
        }
        mt.pwm -= MT_RAMP_STEP;
        motorDrive(mt.dir, (uint16_t)mt.pwm, (uint16_t)mt.pwm);
        break;

    case MT_SWEEP:                                     // stand only: steady speed per level
        motorDrive(mt.dir, mt.level, mt.level);
        if (el < MT_SWEEP_SETTLE_MS) {
            break;
        }
        if (mt.sumL == 0 && mt.sumR == 0 && mt.measStart == 0) {
            mt.measStart = now;
        }
        mt.sumL += aL;
        mt.sumR += aR;
        if (el >= MT_SWEEP_SETTLE_MS + MT_SWEEP_MEASURE_MS) {
            float ms  = (float)(now - mt.measStart + 10);   // include this loop's 10 ms
            float vL  = (float)mt.sumL * 1000.0f / ms / TICKS_PER_CM;
            float vR  = (float)mt.sumR * 1000.0f / ms / TICKS_PER_CM;
            snprintf(line, sizeof(line), "S%c%u L=%.1f R=%.1f cm/s",
                     (mt.dir > 0) ? '+' : '-', mt.level, vL, vR);
            logResult(line);

            mt.sumL = mt.sumR = 0;
            mt.measStart = 0;
            mt.phaseTick = now;
            if (mt.level + MT_SWEEP_STEP > MT_SWEEP_LAST) {
                mt.phase = MT_REST;
                setMotorPwm(0, 0, 0, 0);
            } else {
                mt.level += MT_SWEEP_STEP;
            }
        }
        break;

    case MT_REST:
    default:                                           // coast, then reverse or finish
        setMotorPwm(0, 0, 0, 0);
        if (el >= 800) {
            if (mt.dir > 0) {
                mt.dir = -1;
                mt.phase = MT_SETTLE;
                mt.phaseTick = now;
            } else {
                mt.active = 0;
            }
        }
        break;
    }
}

/* Starts a motor test and blocks until it finishes. A button press stops it.
 *   MF  floor test (robot on the floor): breakaway and stopping PWM, fwd + rev
 *   MS  stand test (rear wheels in the air): breakaway, stopping, and a
 *       PWM -> speed sweep for each wheel, fwd + rev */
static void runMotorTest(char kind)
{
    if (st.active || mt.active) {
        snprintf(st.result, sizeof(st.result), "M ERR busy");
        snprintf(st.oledShort, sizeof(st.oledShort), "M ERR busy");
        return;
    }
    if (kind != 'F' && kind != 'S') {
        snprintf(st.result, sizeof(st.result), "M ERR use MF/MS");
        snprintf(st.oledShort, sizeof(st.oledShort), "M ERR mode");
        return;
    }

    mt.kind      = kind;
    mt.dir       = 1;
    mt.phase     = MT_SETTLE;
    mt.phaseTick = HAL_GetTick();
    mt.measStart = 0;
    mt.goCar[0]  = mt.goCar[1] = 0;
    mt.aborted   = 0;
    testAbort    = 0;
    mt.active    = 1;                                  // motor task takes over

    while (mt.active) {
        if (HAL_GPIO_ReadPin(GPIOE, GPIO_PIN_0) == GPIO_PIN_RESET) {
            testAbort = 1;
        }
        osDelay(20);
    }
    setMotorPwm(0, 0, 0, 0);

    if (mt.aborted) {
        snprintf(st.result, sizeof(st.result), "M%c aborted", kind);
        snprintf(st.oledShort, sizeof(st.oledShort), "M%c ABORTED", kind);
    } else if (kind == 'F') {
        snprintf(st.result, sizeof(st.result), "MF go fwd=%u rev=%u", mt.goCar[0], mt.goCar[1]);
        snprintf(st.oledShort, sizeof(st.oledShort), "F+%u -%u", mt.goCar[0], mt.goCar[1]);
    } else {
        snprintf(st.result, sizeof(st.result), "MS done, send TD");
        snprintf(st.oledShort, sizeof(st.oledShort), "MS done");
    }
    logResult(st.result);
}

/* ============================================================
 * PHASE 3a: STRAIGHT-LINE MOTION
 * ============================================================ */

/* Drives ONE wheel: forward/reverse at pwm, or short-brakes it. */
static void driveWheel(int left, int dir, uint16_t pwm, int brake)
{
    uint16_t fwdPin = 0, revPin = 0;
    if (brake) {
        fwdPin = revPin = PWM_BRAKE;                   // both high = AT8236 brake
    } else if (dir > 0) {
        fwdPin = pwm;
    } else {
        revPin = pwm;
    }
    if (left) {
        __HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_4, fwdPin);
        __HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_3, revPin);
    } else {
        __HAL_TIM_SET_COMPARE(&htim9, TIM_CHANNEL_2, fwdPin);
        __HAL_TIM_SET_COMPARE(&htim9, TIM_CHANNEL_1, revPin);
    }
}

/* One wheel's speed controller. tgtCmS = wanted speed (cm/s, >= 0),
 * cnt = ticks this loop, gain = that wheel's cm/s per PWM in this direction.
 * Returns the PWM magnitude to apply. When braking = 1 the caller is braking
 * both wheels this loop: only the speed filter is updated, and the integral
 * is left alone so it doesn't wind down. */
static uint16_t wheelControl(WheelCtl_t *w, float tgtCmS, int32_t cnt, float gain, int braking)
{
    float meas = (float)abs((int)cnt);
    w->filt = 0.6f * w->filt + 0.4f * meas;

    if (tgtCmS <= 0.0f) {
        w->integ = 0.0f;
        w->still = KICK_AFTER_LOOPS;
        return 0;
    }

    float tgtTicks = tgtCmS * TICKS_PER_CM / 100.0f;
    float e = tgtTicks - w->filt;

    if (braking) {
        w->still = 0;
        return 0;
    }
    w->integ = clampf(w->integ + WHEEL_KI * e, -WHEEL_I_LIMIT, WHEEL_I_LIMIT);

    // Feedforward from the Phase 2 model, then feedback for everything else
    float pwm = MOTOR_FF_BASE + tgtCmS / gain + WHEEL_KP * e + w->integ;

    // Starting takes about twice the power of keeping going: give a burst
    // whenever the wheel should be turning but isn't.
    w->still = (meas == 0.0f) ? (uint8_t)((w->still < 200) ? w->still + 1 : 200) : 0;
    if (w->still >= KICK_AFTER_LOOPS && pwm < MOTOR_KICK_PWM) {
        pwm = MOTOR_KICK_PWM;
    }
    return (uint16_t)clampf(pwm, 0.0f, MOTOR_PWM_MAX);
}

static void writeServo(float us)
{
    __HAL_TIM_SET_COMPARE(&htim12, TIM_CHANNEL_2, (uint16_t)(us + 0.5f));
}

/* One 10 ms step of a straight move. Called ONLY from the motor task. */
static void straightStep(int16_t cntL, int16_t cntR)
{
    uint32_t now = HAL_GetTick();
    uint32_t el  = now - mv.phaseTick;
    float    dist = (float)mv.dir * 0.5f *
                    (float)((left_encoder_val - mv.startL) + (right_encoder_val - mv.startR))
                    / TICKS_PER_CM;

    if (mv.phase != MV_BRAKE && (testAbort || emergency_stop_requested)) {
        mv.aborted = 1;
        mv.phase = MV_BRAKE;
        mv.phaseTick = now;
        el = 0;
    }

    switch (mv.phase) {

    case MV_SETTLE:
        // Wheels straight, always arriving at centre from below (servo slack)
        setMotorPwm(0, 0, 0, 0);
        if (el < mv.settleA) {
            writeServo(SERVO_TRUE_CENTRE + servoTrim - SERVO_APPROACH_US);
        } else {
            writeServo(SERVO_TRUE_CENTRE + servoTrim);
        }
        if (el >= (uint32_t)(mv.settleA + mv.settleB)) {
            mv.startL = left_encoder_val;
            mv.startR = right_encoder_val;
            mv.heading0 = total_angle;
            mv.servoCmd = SERVO_TRUE_CENTRE + servoTrim;
            mv.v = STRAIGHT_V_MIN;
            mv.wl.integ = mv.wr.integ = 0.0f;
            mv.wl.filt  = mv.wr.filt  = 0.0f;
            mv.wl.still = mv.wr.still = KICK_AFTER_LOOPS;   // burst from the first loop
            mv.phase = MV_DRIVE;
            mv.phaseTick = now;
        }
        break;

    case MV_DRIVE: {
        float remaining = mv.target - dist;
        // Brake early enough for the speed we are actually doing: the robot
        // keeps rolling for ~STOP_T seconds after the decision, so the stop
        // point moves back in proportion to speed. Capped at the approach zone
        // so it never brakes early while still slowing down from cruise.
        float vNow     = 0.5f * (mv.wl.filt + mv.wr.filt) * 100.0f / TICKS_PER_CM;
        float stopDist = fminf(STOP_T * vNow, STRAIGHT_APPROACH);
        if (remaining <= stopDist || now > mv.deadline) {
            mv.timedOut = (remaining > stopDist);
            mv.vBrake = vNow;
            mv.phase = MV_BRAKE;
            mv.phaseTick = now;
            setMotorPwm(PWM_BRAKE, PWM_BRAKE, PWM_BRAKE, PWM_BRAKE);
            break;
        }

        // Speed profile: speed up, cruise, slow down so we arrive at V_MIN
        float slowTo = sqrtf(2.0f * STRAIGHT_DECEL *
                       fmaxf(remaining - STRAIGHT_APPROACH, 0.0f)) + STRAIGHT_V_MIN;
        float upTo   = fminf(mv.v + STRAIGHT_ACCEL * 0.01f, STRAIGHT_V_MAX);
        int slowing  = (slowTo < upTo);                // profile is bringing us down to stop
        mv.v = fminf(upTo, slowTo);
        if (mv.v < STRAIGHT_V_MIN) mv.v = STRAIGHT_V_MIN;

        // Heading hold through the servo (+ error = drifted left).
        // P term reacts; servoTrim slowly learns where "straight" really is
        // and is remembered between moves, so it stops re-appearing as error.
        float err = (float)(total_angle - mv.heading0);
        float sgn = (mv.dir > 0) ? 1.0f : -1.0f;       // steering works backwards in reverse
        servoTrim = clampf(servoTrim + HEADING_KI_US * err * sgn, -SERVO_TRIM_MAX, SERVO_TRIM_MAX);
        float off = clampf(HEADING_KP_US * err, -HEADING_MAX_US, HEADING_MAX_US) * sgn;
        float want = SERVO_TRUE_CENTRE + servoTrim + off;
        mv.servoCmd += clampf(want - mv.servoCmd, -SERVO_RATE_US, SERVO_RATE_US);
        writeServo(mv.servoCmd);

        // Both wheels at the same speed; each has its own controller.
        // Cutting power only lets the car coast, which sheds speed too slowly
        // on a smooth floor. If the car as a whole is clearly too fast, brake
        // BOTH wheels for this loop: braking one side alone swings the robot.
        // Only while slowing down for the stop: during cruise, normal speed
        // jiggle on an uneven floor must not trigger braking (that was jerky).
        float tgtTicks  = mv.v * TICKS_PER_CM / 100.0f;
        int   brakeBoth = slowing &&
                          (0.5f * (mv.wl.filt + mv.wr.filt) > tgtTicks + BRAKE_ASSIST_TICKS);
        uint16_t pL = wheelControl(&mv.wl, mv.v, cntL, (mv.dir > 0) ? GAIN_L_FWD : GAIN_L_REV, brakeBoth);
        uint16_t pR = wheelControl(&mv.wr, mv.v, cntR, (mv.dir > 0) ? GAIN_R_FWD : GAIN_R_REV, brakeBoth);
        driveWheel(1, mv.dir, pL, brakeBoth);
        driveWheel(0, mv.dir, pR, brakeBoth);
        break;
    }

    case MV_BRAKE:
    default:
        if (el < 200) {                                // active brake
            setMotorPwm(PWM_BRAKE, PWM_BRAKE, PWM_BRAKE, PWM_BRAKE);
            mv.stillLoops = 0;
            break;
        }
        setMotorPwm(0, 0, 0, 0);
        mv.stillLoops = (cntL == 0 && cntR == 0) ? (uint8_t)(mv.stillLoops + 1) : 0;
        if (mv.stillLoops >= 5 || el > 700) {
            mv.resDist = dist;
            mv.resHead = (float)(total_angle - mv.heading0);
            mv.active = 0;
        }
        break;
    }
}

/* Drives straight by distance_cm (negative = backwards) and blocks until the
 * robot has stopped. Result text is left in mv.result. */
static void runStraight(float distance_cm)
{
    if (mv.active || st.active || mt.active) {
        snprintf(mv.result, sizeof(mv.result), "BUSY");
        return;
    }
    if (fabsf(distance_cm) < 0.5f) {
        snprintf(mv.result, sizeof(mv.result), "d=0.0 h=0.0");
        return;
    }

    // Short servo settle if it is already near centre, longer after a turn
    int curServo = (int)__HAL_TIM_GET_COMPARE(&htim12, TIM_CHANNEL_2);
    int far = abs(curServo - SERVO_TRUE_CENTRE) > 20;

    mv.dir       = (distance_cm > 0.0f) ? 1 : -1;
    mv.target    = fabsf(distance_cm);
    mv.settleA   = far ? 250 : 120;
    mv.settleB   = far ? 200 : 120;
    mv.deadline  = HAL_GetTick() + mv.settleA + mv.settleB + 2000
                   + (uint32_t)(mv.target * 1000.0f / STRAIGHT_V_MIN);
    mv.timedOut  = 0;
    mv.aborted   = 0;
    mv.vBrake    = 0.0f;
    mv.phase     = MV_SETTLE;
    mv.phaseTick = HAL_GetTick();
    testAbort    = 0;
    mv.active    = 1;                                  // motor task takes over

    while (mv.active) {
        if (HAL_GPIO_ReadPin(GPIOE, GPIO_PIN_0) == GPIO_PIN_RESET) {
            testAbort = 1;                             // user button: stop now
        }
        osDelay(10);
    }

    snprintf(mv.result, sizeof(mv.result), "d=%.1f h=%.1f vb=%.0f%s",
             mv.resDist, mv.resHead, mv.vBrake,
             mv.aborted ? " ABORT" : (mv.timedOut ? " TO" : ""));
}

/* Field tool: knob position -> distance, -100..+100 cm in 10 cm steps. */
static int knobDistanceCm(uint16_t us)
{
    int d = (int)lroundf(((float)us - 1550.0f) / 650.0f * 10.0f) * 10;
    if (d > 100)  d = 100;
    if (d < -100) d = -100;
    return d;
}

/* Reads one ADC1 channel. Only ever called from the OLED task, so the
 * battery and the knob never fight over the ADC. Returns 0xFFFFFFFF on failure. */
static uint32_t adcReadChannel(uint32_t channel)
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

/* Potentiometer on PB1 -> servo us, rounded to 5 us. The pot's usable raw
 * range depends on the board wiring, so it learns its own end stops:
 * turn the knob fully both ways once after power-up. */
static void updateKnob(void)
{
    static float filt = -1.0f;
    static uint32_t rawMin = 4095, rawMax = 0;
    static int lastRounded = -1;
    uint32_t raw = adcReadChannel(ADC_CHANNEL_9);
    dash_knobRaw = (raw == 0xFFFFFFFFu) ? -1 : (int32_t)raw;
    if (raw == 0xFFFFFFFFu) return;

    if (raw < rawMin) rawMin = raw;
    if (raw > rawMax) rawMax = raw;
    if (rawMax - rawMin < 200) return;                 // not enough travel seen yet

    filt = (filt < 0.0f) ? (float)raw : filt * 0.7f + (float)raw * 0.3f;
    float frac = (filt - (float)rawMin) / (float)(rawMax - rawMin);
    float us = KNOB_US_MIN + frac * (float)(KNOB_US_MAX - KNOB_US_MIN);
    uint16_t rounded = (uint16_t)(((int)(us + 2.5f) / 5) * 5);

    if (lastRounded < 0 || abs((int)rounded - lastRounded) >= 5) {   // knob physically moved
        lastRounded = rounded;
        dash_knobUs = rounded;
    }
}

/* Sets PB1 to analog so the pot can be read. Done in code so the .ioc
 * doesn't need changing. Call once before the scheduler starts. */
static void knobPinInit(void)
{
    GPIO_InitTypeDef g = {0};
    __HAL_RCC_GPIOB_CLK_ENABLE();
    g.Pin  = GPIO_PIN_1;
    g.Mode = GPIO_MODE_ANALOG;
    g.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOB, &g);
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
#if PHASE1_FIELD_TOOL
  /* PHASE 1 FIELD TOOL - no cable needed while driving.
   *   Knob   : sets the servo; the wheels follow it live while idle
   *   Tap    : run the current test at the knob value (1.5 s delay to let go)
   *   Hold   : cycle modes SD (straight drive) -> TA (arc) -> TC (centre)
   *            -> MF (motor floor) -> MS (motor stand, knob fully CW) -> SD
   *   SD     : knob picks a distance, -100..+100 cm in 10 cm steps
   *   Tap during any test: stop it
   *   OLED   : CMD line shows "TA 1540" etc, then the result after a run
   *   "TD"   : over serial afterwards, prints every result from this power-up
   *   "TV<us>": over serial, sets the value instead of the knob (then unplug)
   *   k####  : raw knob reading on the CMD line, for diagnosis */
  char     fieldMode   = 'D';   // PHASE 3a: start in drive-test mode
  uint8_t  showResult  = 0;
  uint16_t resultUs    = 0;

  for(;;)
  {
      uint16_t us = dash_knobUs;
      int motorMode = (fieldMode == 'F' || fieldMode == 'S');

      if (!st.active && !mt.active && !mv.active && !is_moving) {
          // Live steering in the servo modes; wheels straight in the motor modes.
          // In SD mode the servo is left alone: the drive code owns it.
          if (fieldMode == 'A' || fieldMode == 'C') {
              __HAL_TIM_SET_COMPARE(&htim12, TIM_CHANNEL_2, us);
          } else if (motorMode) {
              __HAL_TIM_SET_COMPARE(&htim12, TIM_CHANNEL_2, SERVO_TRUE_CENTRE);
          }
      }

      if (showResult && abs((int)us - (int)resultUs) >= 10) {
          showResult = 0;                              // knob moved: back to live view
      }
      if (!showResult && fieldMode == 'D') {
          snprintf(dash_lastCmd, sizeof(dash_lastCmd), "SD %+d", knobDistanceCm(us));
      } else if (!showResult && fieldMode == 'F') {
          strcpy(dash_lastCmd, "MF floor");
      } else if (!showResult && fieldMode == 'S') {
          strcpy(dash_lastCmd, (us <= MT_STAND_ARM_US) ? "MS ready" : "MS knob CW");
      } else if (!showResult) {
          if (dash_knobRaw >= 0) {
              snprintf(dash_lastCmd, sizeof(dash_lastCmd), "T%c%u k%ld", fieldMode, us, (long)dash_knobRaw);
          } else {
              snprintf(dash_lastCmd, sizeof(dash_lastCmd), "T%c%u k--", fieldMode, us);
          }
      }

      if (HAL_GPIO_ReadPin(GPIOE, GPIO_PIN_0) == GPIO_PIN_RESET) {
          uint32_t t0 = HAL_GetTick();
          while (HAL_GPIO_ReadPin(GPIOE, GPIO_PIN_0) == GPIO_PIN_RESET) {
              osDelay(20);
          }
          uint32_t held = HAL_GetTick() - t0;

          if (held >= BTN_LONG_MS) {                  // hold: next mode
              fieldMode = (fieldMode == 'D') ? 'A'
                        : (fieldMode == 'A') ? 'C'
                        : (fieldMode == 'C') ? 'F'
                        : (fieldMode == 'F') ? 'S' : 'D';
              showResult = 0;
          } else if (held >= 40) {                     // tap: run
              if (fieldMode == 'S' && us > MT_STAND_ARM_US) {
                  strcpy(dash_lastCmd, "MS knob CW!");  // safety interlock
                  osDelay(1000);
              } else if (fieldMode == 'D') {
                  int d = knobDistanceCm(us);
                  snprintf(dash_lastCmd, sizeof(dash_lastCmd), "SD %+d GO", d);
                  osDelay(1500);
                  if (d != 0) {
                      char line[48];
                      runStraight((float)d);
                      snprintf(line, sizeof(line), "SD%+d %s", d, mv.result);
                      logResult(line);
                      snprintf(dash_lastCmd, sizeof(dash_lastCmd), "%.14s", mv.result);
                  }
                  resultUs = us;
                  showResult = 1;
                  while (HAL_GPIO_ReadPin(GPIOE, GPIO_PIN_0) == GPIO_PIN_RESET) {
                      osDelay(20);
                  }
                  osDelay(200);
              } else {
                  if (motorMode) {
                      snprintf(dash_lastCmd, sizeof(dash_lastCmd), "M%c GO", fieldMode);
                  } else {
                      snprintf(dash_lastCmd, sizeof(dash_lastCmd), "T%c %u GO", fieldMode, us);
                  }
                  osDelay(1500);
                  if (motorMode) {
                      runMotorTest(fieldMode);
                  } else {
                      runServoTest(fieldMode, us);
                  }
                  strcpy(dash_lastCmd, st.oledShort);
                  resultUs = us;
                  showResult = 1;
                  // If the button stopped the test, don't read that press as a new tap
                  while (HAL_GPIO_ReadPin(GPIOE, GPIO_PIN_0) == GPIO_PIN_RESET) {
                      osDelay(20);
                  }
                  osDelay(200);
              }
          }
      }

      HAL_GPIO_TogglePin(LED3_GPIO_Port, LED3_Pin);
      osDelay(50);
  }
#else
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
              case 1: strcpy(test_name, "L90");  is_left = 1; test_angle = 90.0;  break;
              case 2: strcpy(test_name, "S100"); is_left = 0; test_angle = 0.0; break;
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
        	  if (test_angle > 0)
        		  moveCarRight(test_angle);
        	  else
        		  moveCarStraight(100);
          }

          // 4. Test is complete. Show a completion message.
          strcpy(oled_status_msg, "Done. Press btn.");

          // Wait for a button press to acknowledge and return to the dashboard
          while(HAL_GPIO_ReadPin(GPIOE, GPIO_PIN_0) != GPIO_PIN_RESET) {
              osDelay(100);
          }

          // 5. Advance the state machine for the next button press
          test_step++;
          if (test_step > 3) {
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
#endif  /* PHASE1_FIELD_TOOL */
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
				int usedNewStraight = 0;   // PHASE 3a: ACK carries the move result

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
								// "SB<cm>": backwards straight (PHASE 3a engine)
								runStraight(-(float)value);
								usedNewStraight = 1;
							} else {
								// "SF<cm>" or "S<cm>": forwards straight (PHASE 3a engine)
								runStraight((float)value);
								usedNewStraight = 1;
							}
							break;

						case 'F':								// Forward (Alias for Straight)
							runStraight((float)value);
							usedNewStraight = 1;
							break;

						case 'B':								// Backward (Alias for negative Straight)
							runStraight(-(float)value);
							usedNewStraight = 1;
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

						case 'T':   // PHASE 1 servo tests: TS / TC / TA + servo us
							runServoTest(command_char2, value);
							break;

						case 'M':   // PHASE 2 motor tests: MF (floor) / MS (stand)
							runMotorTest(command_char2);
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
				} else if (usedNewStraight) {
					// PHASE 3a: distance travelled and heading change
					snprintf(ackMsg, sizeof(ackMsg), "A %s\n", mv.result);
				} else if (command_char1 == 'T' || command_char1 == 'M') {
					// PHASE 1 test result
					snprintf(ackMsg, sizeof(ackMsg), "A %s\n", st.result);
				} else {
					// Standard ACK
					snprintf(ackMsg, sizeof(ackMsg), "A\n");
				}
				HAL_UART_Transmit(&huart3, (uint8_t *)ackMsg, strlen(ackMsg), 100);

				// 4. UPDATE DASHBOARD AND RESET
				if (command_char1 == 'T' || command_char1 == 'M') {
					strcpy(dash_lastCmd, st.oledShort);     // PHASE 1: show the result instead
				} else {
					strcpy(dash_lastCmd, cmdBuffer);
				}
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

	// PHASE 3a: the robot can't rotate if neither wheel has turned for 0.5 s.
	// The gyro task uses this to keep its zero-rate offset up to date.
	{
		static uint16_t stillCount = 0;
		if (cnt_L == 0 && cnt_R == 0 && !mv.active && !st.active && !mt.active && !is_moving) {
			if (stillCount < 1000) stillCount++;
		} else {
			stillCount = 0;
		}
		robotStill = (stillCount >= 50);
	}

	// ---------------------------------------------------------
	// PHASE 1 TEST HOOK: a servo test owns the motors while active
	// ---------------------------------------------------------
	if (mv.active) {                   // PHASE 3a straight move
		straightStep(cnt_L, cnt_R);
		osDelay(10);
		continue;
	}
	if (mt.active) {                   // PHASE 2 motor test
		motorTestStep(cnt_L, cnt_R);
		osDelay(10);
		continue;
	}
	if (st.active) {
		servoTestStep(cnt_L, cnt_R);
		osDelay(10);
		continue;
	}

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
		sprintf(textBuffer, "G: %.1f D: %.1fcm", (float)dash_gyroZ, dash_ultraDist);
		OLED_ShowString(0, 12, (uint8_t*) textBuffer);

		// Line 3: Accumulated Left Encoder
		sprintf(textBuffer, "Enc L: %d", dash_encoderL);
		OLED_ShowString(0, 24, (uint8_t *) textBuffer);

		// Line 4: Accumulated Right Encoder
		sprintf(textBuffer, "Enc R: %d", dash_encoderR);
		OLED_ShowString(0, 36, (uint8_t *) textBuffer);

		// Line 5: Raw Direction Indicator & Battery Percentage
		dash_battV = dash_battV * 0.9f + readBatteryVoltage() * 0.1f;   // smooth out load sag
		updateKnob();   // PHASE 1: potentiometer -> servo us
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

	if (robotStill) {
		// PHASE 3a: the wheels haven't turned for 0.5 s, so the robot isn't
		// rotating. Hold the heading and use the reading to track the
		// gyro's zero-rate offset, which drifts as the sensor warms up.
		offset += 0.002 * (gz_raw - offset);
		gz_corrected = 0.0;
	} else {
		// PHASE 3a: no deadband any more. The old 0.35 deg/s deadband hid
		// drifts of up to ~1.8 deg/m, which the heading hold must see.
		gz_corrected = gz_raw - offset;
	}

	// 4. Integrate the filtered velocity to get absolute heading
	total_angle += gz_corrected * dt;
	dash_gyroRate = (float)gz_corrected;   // PHASE 1: yaw rate for the servo test

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

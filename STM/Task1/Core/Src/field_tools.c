/*
 * field_tools.c
 *
 * Knob-and-button calibration modes (SD straights, TN turns), the TD result
 * log and the TV command. See field_tools.h. Compiled only with FIELD_TOOLS = 1.
 */
#include "main.h"
#include "cmsis_os.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "robot_config.h"
#include "motion.h"
#include "field_tools.h"

#if FIELD_TOOLS

/* ---- Owned by main.c ------------------------------------------------------ */
extern UART_HandleTypeDef huart3;
extern char dash_lastCmd[15];                   // OLED "CMD:" line

/* ---- Button on PE0 (pressed = low) ---------------------------------------- */
#define BUTTON_PRESSED()  (HAL_GPIO_ReadPin(GPIOE, GPIO_PIN_0) == GPIO_PIN_RESET)

static volatile uint16_t knobUs = 1500;         // knob position mapped to 900..2200 (or set by TV)
static char    logLines[TEST_LOG_SIZE][80];     // ring buffer of results for TD
static uint8_t logHead  = 0;                    // next slot to write
static uint8_t logCount = 0;                    // number of valid entries

/* Turn list for TN mode: the knob picks one of these. */
static const char *const TURN_TESTS[] = {
    "LF090", "RF090", "LB090", "RB090",
    "LF045", "RF045", "LB045", "RB045",
    "LF180", "RF180", "LF010", "RF010",
    "LF003", "RF003"
};
#define TURN_TEST_COUNT  ((int)(sizeof(TURN_TESTS) / sizeof(TURN_TESTS[0])))

/* Appends one line to the result log that "TD" prints. */
static void logResult(const char *line)
{
    strncpy(logLines[logHead], line, sizeof(logLines[0]) - 1);
    logLines[logHead][sizeof(logLines[0]) - 1] = '\0';
    logHead = (uint8_t)((logHead + 1) % TEST_LOG_SIZE);
    if (logCount < TEST_LOG_SIZE) logCount++;
}

static int knobTurnIndex(uint16_t us)
{
    int i = (int)(((float)us - (float)KNOB_US_MIN) * TURN_TEST_COUNT
                  / (float)(KNOB_US_MAX - KNOB_US_MIN + 1));
    if (i < 0) i = 0;
    if (i >= TURN_TEST_COUNT) i = TURN_TEST_COUNT - 1;
    return i;
}

/* Knob position -> distance, -100..+100 cm in 10 cm steps. */
static int knobDistanceCm(uint16_t us)
{
    int d = (int)lroundf(((float)us - 1550.0f) / 650.0f * 10.0f) * 10;
    if (d > 100)  d = 100;
    if (d < -100) d = -100;
    return d;
}

/* Runs a turn given as text, e.g. "LB045". */
static void runTurnText(const char *cmd)
{
    Motion_Turn(cmd[0] == 'L', (cmd[1] == 'B') ? -1 : 1, (float)atoi(cmd + 2));
}

/* Sets PB1 to analog so the knob (potentiometer) can be read. Done in code
 * so the .ioc doesn't need changing. Call once before the scheduler starts. */
void FieldTools_Init(void)
{
    GPIO_InitTypeDef g = {0};
    __HAL_RCC_GPIOB_CLK_ENABLE();
    g.Pin  = GPIO_PIN_1;
    g.Mode = GPIO_MODE_ANALOG;
    g.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOB, &g);
}

/* Potentiometer on PB1 -> servo us, rounded to 5 us. The pot's usable raw
 * range depends on the board wiring, so it learns its own end stops:
 * turn the knob fully both ways once after power-up. */
void FieldTools_UpdateKnob(void)
{
    static float filt = -1.0f;
    static uint32_t rawMin = 4095, rawMax = 0;
    static int lastRounded = -1;
    uint32_t raw = Board_ReadAdc(ADC_CHANNEL_9);
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
        knobUs = rounded;
    }
}

/* Serial "T" commands. Reply text goes in reply (the caller adds "A "). */
int FieldTools_Command(char sub, int value, char *reply, size_t replyLen)
{
    if (sub == 'D') {                                  // TD: print the result log
        char line[100];
        uint8_t first = (uint8_t)((logHead + TEST_LOG_SIZE - logCount) % TEST_LOG_SIZE);
        for (uint8_t i = 0; i < logCount; i++) {
            snprintf(line, sizeof(line), "%2u %s\r\n", i + 1,
                     logLines[(first + i) % TEST_LOG_SIZE]);
            HAL_UART_Transmit(&huart3, (uint8_t *)line, strlen(line), 100);
        }
        snprintf(reply, replyLen, "TD %u results", logCount);
        return 1;
    }
    if (sub == 'V') {                                  // TV: set the knob value by hand
        if (value < TV_MIN || value > TV_MAX) {
            snprintf(reply, replyLen, "T ERR %d out of range", value);
        } else {
            knobUs = (uint16_t)value;
            snprintf(reply, replyLen, "TV%d set", value);
        }
        return 1;
    }
    return 0;
}

/* Body of the default task. Never returns.
 *   Knob : picks the distance (SD) or the turn (TN); the OLED CMD line shows it
 *   Tap  : runs it after 1.5 s (time to let go); tap again during a move to stop
 *   Hold : ~1 s switches between SD and TN
 * Each run first makes the robot's current direction "north", because it has
 * been picked up and placed by hand. Every result goes into the TD log. */
void FieldTools_Run(void)
{
    char     fieldMode  = 'D';                         // 'D' = SD straights, 'U' = TN turns
    uint8_t  showResult = 0;
    uint16_t resultUs   = 0;

    for (;;)
    {
        uint16_t us = knobUs;

        if (showResult && abs((int)us - (int)resultUs) >= 10) {
            showResult = 0;                            // knob moved: back to the live view
        }
        if (!showResult) {
            if (fieldMode == 'D') {
                snprintf(dash_lastCmd, sizeof(dash_lastCmd), "SD %+d", knobDistanceCm(us));
            } else {
                snprintf(dash_lastCmd, sizeof(dash_lastCmd), "TN %s", TURN_TESTS[knobTurnIndex(us)]);
            }
        }

        if (BUTTON_PRESSED()) {
            uint32_t t0 = HAL_GetTick();
            while (BUTTON_PRESSED()) {
                osDelay(20);
            }
            uint32_t held = HAL_GetTick() - t0;

            if (held >= BTN_LONG_MS) {                 // hold: switch mode
                fieldMode = (fieldMode == 'D') ? 'U' : 'D';
                showResult = 0;
            } else if (held >= 40) {                   // tap: run
                char line[96];
                if (fieldMode == 'U') {
                    const char *t = TURN_TESTS[knobTurnIndex(us)];
                    snprintf(dash_lastCmd, sizeof(dash_lastCmd), "TN %s GO", t);
                    osDelay(1500);
                    Motion_ZeroHeading();              // robot was repositioned by hand
                    runTurnText(t);
                    snprintf(line, sizeof(line), "%s %s", t, Motion_LogText());
                    logResult(line);
                    snprintf(dash_lastCmd, sizeof(dash_lastCmd), "%.14s", Motion_LogText());
                } else {
                    int d = knobDistanceCm(us);
                    snprintf(dash_lastCmd, sizeof(dash_lastCmd), "SD %+d GO", d);
                    osDelay(1500);
                    if (d != 0) {
                        Motion_ZeroHeading();          // robot was repositioned by hand
                        Motion_Straight((float)d);
                        snprintf(line, sizeof(line), "SD%+d %s", d, Motion_LogText());
                        logResult(line);
                        snprintf(dash_lastCmd, sizeof(dash_lastCmd), "%.14s", Motion_LogText());
                    }
                }
                resultUs = us;
                showResult = 1;
                // If the button stopped the move, don't read that press as a new tap
                while (BUTTON_PRESSED()) {
                    osDelay(20);
                }
                osDelay(200);
            }
        }

        HAL_GPIO_TogglePin(LED3_GPIO_Port, LED3_Pin);
        osDelay(50);
    }
}

#endif /* FIELD_TOOLS */

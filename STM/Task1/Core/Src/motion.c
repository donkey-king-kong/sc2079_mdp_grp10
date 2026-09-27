/*
 * motion.c
 *
 *  Created on: Sep 27, 2026
 *      Author: Ryan Tan
 *
 * Straights, turns and absolute heading. See motion.h for how the pieces fit.
 * All tunable values live in robot_config.h.
 *
 * Straights: per-wheel speed control with a start burst, a speed profile
 * (speed up, cruise, slow approach), servo heading hold with centre learning,
 * line holding, steering-play compensation, and a speed-dependent brake point.
 *
 * Turns: servo to full lock, rear wheels at the measured inner/outer ratio,
 * stop on the gyro, braking early for the current turning speed.
 *
 * Heading: absolute for the whole run. ZH sets "north"; every turn moves the
 * target by exactly the commanded angle and every straight holds it, so an
 * error in one move is corrected by the next instead of accumulating.
 */
#include "main.h"
#include "cmsis_os.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "robot_config.h"
#include "motion.h"

/* ---- Owned by main.c ------------------------------------------------------ */
extern TIM_HandleTypeDef htim4;                 // left motor PWM
extern TIM_HandleTypeDef htim9;                 // right motor PWM
extern TIM_HandleTypeDef htim12;                // steering servo
extern int32_t left_encoder_val;                // running encoder totals (motor task)
extern int32_t right_encoder_val;
extern volatile float dash_gyroRate;            // yaw rate, deg/s, CCW positive (gyro task)
extern volatile uint8_t emergency_stop_requested;   // set by '!' on the UART

/* ---- Button on PE0 (pressed = low) stops the current move ----------------- */
#define BUTTON_PRESSED()  (HAL_GPIO_ReadPin(GPIOE, GPIO_PIN_0) == GPIO_PIN_RESET)

/* ---- Move phases ---------------------------------------------------------- */
#define MV_SETTLE   0       // straight: servo settling
#define MV_DRIVE    1
#define MV_BRAKE    2
#define TN_SETTLE   0       // turn: servo swinging to lock
#define TN_DRIVE    1
#define TN_BRAKE    2

/* ---- Per-wheel speed controller state ------------------------------------ */
typedef struct {
    float   integ;                // Integral term (PWM)
    float   filt;                 // Filtered speed, ticks per 10 ms
    uint8_t still;                // Loops in a row with no ticks
} WheelCtl_t;

/* ---- Straight move -------------------------------------------------------- */
typedef struct {
    volatile uint8_t active;      // 1 while a straight move owns the motors
    uint8_t  phase;               // MV_SETTLE / MV_DRIVE / MV_BRAKE
    uint32_t phaseTick;
    int8_t   dir;                 // +1 forward, -1 backward
    float    target;              // Distance to travel, cm (always positive)
    float    v;                   // Current profile speed, cm/s
    int32_t  startL, startR;      // Encoder totals when driving began
    double   heading0;            // Heading to hold (deg)
    float    lateral;             // Estimated sideways drift from the start line (cm, + = left)
    float    wheelEst;            // Where the wheels actually point, in servo us (play model)
    float    distPrev;            // Distance travelled at the previous loop (cm)
    float    servoCmd;            // Current servo command (us)
    uint16_t settleA, settleB;    // Servo settle times (ms): below-centre, then centre
    uint32_t deadline;            // Timeout tick
    uint8_t  timedOut;
    uint8_t  aborted;
    uint8_t  stillLoops;          // Used while waiting to stop after braking
    WheelCtl_t wl, wr;
    float    resDist, resHead;    // Results: distance travelled, final heading error
    float    vBrake;              // Diagnostic: speed when braking started (cm/s)
    float    eBrake;              // Diagnostic: heading error when braking started (deg)
    float    eEarly;              // Diagnostic: largest heading error in the first 20 cm (deg)
    uint16_t loops;               // Diagnostic: loops since driving began
    uint16_t startLoopL;          // Diagnostic: loop when each wheel first turned
    uint16_t startLoopR;
} Move_t;

/* ---- Turn ------------------------------------------------------------------ */
typedef struct {
    volatile uint8_t active;      // 1 while a turn owns the motors
    uint8_t  phase;               // TN_SETTLE / TN_DRIVE / TN_BRAKE
    uint32_t phaseTick;
    int8_t   dir;                 // +1 forward, -1 reverse
    int8_t   left;                // 1 = left lock, 0 = right lock
    int8_t   hsign;               // +1 if the heading should increase (CCW), -1 if decrease
    float    target;              // Heading change wanted (degrees, positive)
    float    radius;              // Turning radius for this lock (cm)
    float    io;                  // Inner/outer wheel speed ratio for this lock
    uint16_t lockUs;              // Servo lock value
    uint16_t settleMs;            // Servo settle time before driving
    float    v;                   // Current centre speed (cm/s)
    double   heading0;            // Heading when driving began
    uint32_t deadline;
    uint8_t  timedOut;
    uint8_t  aborted;
    uint8_t  stillLoops;
    WheelCtl_t wl, wr;
    float    resAngle;            // Heading change actually achieved (degrees)
    float    targetAbs;           // Absolute heading to stop at (gyro frame)
    float    resErr;              // Final heading minus targetAbs (deg, + = left of target)
    float    wBrake;              // Diagnostic: yaw rate when braking started (deg/s)
    float    wFilt;               // Yaw rate averaged over a few readings (deg/s)
    float    vBrake;              // Diagnostic: wheel speed when braking started (cm/s)
} Turn_t;

static Move_t mv = {0};
static Turn_t tn = {0};

/* ---- Heading ---------------------------------------------------------------- */
volatile float   headingDeg    = 0.0f;   // gyro heading (written by the gyro task)
float            headingZero   = 0.0f;   // gyro heading at the last ZH ("north")
static float     headingTarget = 0.0f;   // heading the robot should be facing (gyro frame)
static uint8_t   headingSet    = 0;      // 0 until ZH or the first move sets "north"
volatile uint8_t robotStill    = 0;      // 1 when the wheels haven't turned for 0.5 s
static float     servoTrim     = 0.0f;   // learned centre correction (us), kept between moves
static volatile uint8_t abortReq = 0;    // button pressed during a move

/* ---- Results of the last move ----------------------------------------------- */
static char replyShort[48] = "";
static char replyLong[80]  = "";

static void setResults(const char *shortText, const char *longText)
{
    snprintf(replyShort, sizeof(replyShort), "%s", shortText);
    snprintf(replyLong, sizeof(replyLong), "%s", longText);
}

const char *Motion_Reply(void)
{
#if DEBUG_REPLIES
    return replyLong;
#else
    return replyShort;
#endif
}

const char *Motion_LogText(void)
{
    return replyLong;
}

uint8_t Motion_IsBusy(void)
{
    return (uint8_t)(mv.active || tn.active);
}

/* ==========================================================================
 *  Low-level helpers
 * ========================================================================== */

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

/* ==========================================================================
 *  Heading
 * ========================================================================== */

/* "ZH": make the robot's current direction "north" for this run. */
void Motion_ZeroHeading(void)
{
    headingZero   = headingDeg;
    headingTarget = headingDeg;
    headingSet    = 1;
}

/* ==========================================================================
 *  Straights
 * ========================================================================== */

/* One 10 ms step of a straight move. Called only from Motion_Step(). */
static void straightStep(int16_t cntL, int16_t cntR)
{
    uint32_t now = HAL_GetTick();
    uint32_t el  = now - mv.phaseTick;
    float    dist = (float)mv.dir * 0.5f *
                    (float)((left_encoder_val - mv.startL) + (right_encoder_val - mv.startR))
                    / TICKS_PER_CM;

    if (mv.phase != MV_BRAKE && (abortReq || emergency_stop_requested)) {
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
            mv.heading0 = headingTarget;               // hold the run's absolute heading
            mv.lateral  = 0.0f;
            mv.distPrev = 0.0f;
            mv.eEarly   = 0.0f;
            mv.loops    = 0;
            mv.startLoopL = mv.startLoopR = 0;
            mv.servoCmd = SERVO_TRUE_CENTRE + servoTrim;
            // Settled from below, so the wheels sit at the low side of the play
            mv.wheelEst = mv.servoCmd - 0.5f * STEER_PLAY_US;
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
            mv.eBrake = headingDeg - (float)mv.heading0;
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
        float err = headingDeg - (float)mv.heading0;
        float sgn = (mv.dir > 0) ? 1.0f : -1.0f;       // steering works backwards in reverse

        // Diagnostics: how big the early twist is, and which wheel got going first
        mv.loops++;
        if (!mv.startLoopL && cntL != 0) mv.startLoopL = mv.loops;
        if (!mv.startLoopR && cntR != 0) mv.startLoopR = mv.loops;
        if (dist < 20.0f && fabsf(err) > fabsf(mv.eEarly)) mv.eEarly = err;

        // Line holding: the gyro can't see sideways drift, but it can be
        // added up - every bit of travel while the heading is off moves the
        // robot sideways by (distance x sin(heading error)). Steer so the
        // robot aims back onto the start line, not just parallel to it.
        float ds = (dist - mv.distPrev) * sgn;         // signed: negative when reversing
        mv.distPrev = dist;
        mv.lateral += ds * sinf(err * DEG2RAD);
        // Fade the line holding out over the last STRAIGHT_KY_FADE cm, so the
        // robot finishes pointing straight rather than angled mid-correction.
        float fade = clampf(remaining / STRAIGHT_KY_FADE, 0.0f, 1.0f);
        float steerErr = err + sgn * STRAIGHT_KY * fade * mv.lateral;

        servoTrim = clampf(servoTrim + HEADING_KI_US * err * sgn, -SERVO_TRIM_MAX, SERVO_TRIM_MAX);
        float off = clampf(HEADING_KP_US * steerErr, -HEADING_MAX_US, HEADING_MAX_US) * sgn;
        if (off > 0.0f) {
            off *= STEER_RIGHT_GAIN;                   // higher us = steer right = the weaker side
        }
        float want = SERVO_TRUE_CENTRE + servoTrim + off;
        mv.servoCmd += clampf(want - mv.servoCmd, -SERVO_RATE_US, SERVO_RATE_US);

        if (STEER_PLAY_US > 0.0f) {
            // Backlash compensation. servoCmd is where we want the WHEELS.
            // 1500 + trim was found approaching from below, so in wheel terms
            // "straight" is half the play lower; shift the wheel target to match.
            float wheelWant = mv.servoCmd - 0.5f * STEER_PLAY_US;
            float half = 0.5f * STEER_PLAY_US;
            float servoOut;
            if (wheelWant > mv.wheelEst) {             // pushing right: servo leads by half the play
                mv.wheelEst = wheelWant;
                servoOut = wheelWant + half;
            } else if (wheelWant < mv.wheelEst) {      // pushing left: servo leads the other way
                mv.wheelEst = wheelWant;
                servoOut = wheelWant - half;
            } else {
                servoOut = __HAL_TIM_GET_COMPARE(&htim12, TIM_CHANNEL_2);
            }
            writeServo(servoOut);
        } else {
            writeServo(mv.servoCmd);
        }

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
            mv.resHead = headingDeg - (float)mv.heading0;   // final error vs target
            mv.active = 0;
        }
        break;
    }
}

/* Drives straight by distance_cm (negative = backwards) and blocks until the
 * robot has stopped. Results: Motion_Reply() / Motion_LogText(). */
void Motion_Straight(float distance_cm)
{
    if (mv.active || tn.active) {
        setResults("BUSY", "BUSY");
        return;
    }
    if (fabsf(distance_cm) < 0.5f) {
        setResults("d=0.0 e=+0.0", "d=0.0 e=+0.0");
        return;
    }

    if (!headingSet) {
        Motion_ZeroHeading();                          // no ZH received - this is "north"
    }
    emergency_stop_requested = 0;                      // a '!' from before this move doesn't count

    // Short servo settle if it is already near centre (including the
    // just-below-centre position a turn leaves it at), longer otherwise
    int curServo = (int)__HAL_TIM_GET_COMPARE(&htim12, TIM_CHANNEL_2);
    int ctr = (int)(SERVO_TRUE_CENTRE + servoTrim);
    int far = (curServo < ctr - SERVO_APPROACH_US - 5) || (curServo > ctr + 20);

    mv.dir       = (distance_cm > 0.0f) ? 1 : -1;
    mv.target    = fabsf(distance_cm);
    mv.settleA   = far ? 250 : 120;
    mv.settleB   = far ? 200 : 120;
    mv.deadline  = HAL_GetTick() + mv.settleA + mv.settleB + 2000
                   + (uint32_t)(mv.target * 1000.0f / STRAIGHT_V_MIN);
    mv.timedOut  = 0;
    mv.aborted   = 0;
    mv.vBrake    = 0.0f;
    mv.eBrake    = 0.0f;
    mv.phase     = MV_SETTLE;
    mv.phaseTick = HAL_GetTick();
    abortReq     = 0;
    mv.active    = 1;                                  // motor task takes over

    while (mv.active) {
        if (HAL_GPIO_ReadPin(GPIOE, GPIO_PIN_0) == GPIO_PIN_RESET) {
            abortReq = 1;                              // user button: stop now
        }
        osDelay(10);
    }

    const char *end = mv.aborted ? " ABORT" : (mv.timedOut ? " TO" : "");
    snprintf(replyShort, sizeof(replyShort), "d=%.1f e=%+.1f%s", mv.resDist, mv.resHead, end);
    // Full diagnostics: eb = heading error when braking began, y = estimated
    // sideways drift (cm), m = largest heading error in the first 20 cm,
    // s = which wheel started first (+ = left, in 10 ms loops),
    // vb = speed when braking began (cm/s), t = learned centre trim (us)
    snprintf(replyLong, sizeof(replyLong), "d=%.1f e=%+.1f eb=%+.1f y=%+.1f m=%+.1f s=%+d vb=%.0f t=%+.0f%s",
             mv.resDist, mv.resHead, mv.eBrake, mv.lateral, mv.eEarly,
             (int)mv.startLoopR - (int)mv.startLoopL,
             mv.vBrake, servoTrim, end);
}

/* ==========================================================================
 *  Turns
 * ========================================================================== */

/* One 10 ms step of a turn. Called only from Motion_Step(). */
static void turnStep(int16_t cntL, int16_t cntR)
{
    uint32_t now    = HAL_GetTick();
    uint32_t el     = now - tn.phaseTick;
    float    turned = (headingDeg - (float)tn.heading0) * (float)tn.hsign;   // rotation this turn (deg)

    if (tn.phase != TN_BRAKE && (abortReq || emergency_stop_requested)) {
        tn.aborted = 1;
        tn.phase = TN_BRAKE;
        tn.phaseTick = now;
        el = 0;
    }

    switch (tn.phase) {

    case TN_SETTLE:                                    // wheels to full lock, motors off
        setMotorPwm(0, 0, 0, 0);
        writeServo(tn.lockUs);
        if (el >= tn.settleMs) {
            tn.heading0 = headingDeg;
            tn.v = TURN_V_MIN;
            tn.wl.integ = tn.wr.integ = 0.0f;
            tn.wl.filt  = tn.wr.filt  = 0.0f;
            tn.wl.still = tn.wr.still = KICK_AFTER_LOOPS;   // start burst from the first loop
            tn.wFilt = 0.0f;
            tn.phase = TN_DRIVE;
            tn.phaseTick = now;
        }
        break;

    case TN_DRIVE: {
        // Measured to the absolute target, so any error left over
        // from earlier moves is absorbed here instead of carried forward.
        float remaining = (tn.targetAbs - headingDeg) * (float)tn.hsign;
        // Yaw rate averaged over a few readings, so a one-instant jolt (e.g.
        // the front-left wheel skidding and catching) can't fake a high rate.
        tn.wFilt = (1.0f - TURN_W_ALPHA) * tn.wFilt + TURN_W_ALPHA * fabsf(dash_gyroRate);
        float wNow      = tn.wFilt;

        // Same idea as straights: the robot keeps turning for ~TURN_STOP_T s
        // after the decision, so brake earlier the faster it is turning.
        // Capped at the final approach zone (2 cm of arc).
        float approachDeg = TURN_APPROACH_CM / tn.radius / DEG2RAD;
        float stopDeg     = fminf(TURN_STOP_T * wNow, approachDeg);
        if (remaining <= stopDeg || now > tn.deadline) {
            tn.timedOut = (remaining > stopDeg);
            tn.wBrake = wNow;
            tn.vBrake = 0.5f * (tn.wl.filt + tn.wr.filt) * 100.0f / TICKS_PER_CM;
            tn.phase = TN_BRAKE;
            tn.phaseTick = now;
            setMotorPwm(PWM_BRAKE, PWM_BRAKE, PWM_BRAKE, PWM_BRAKE);
            break;
        }

        // Speed profile along the arc, exactly like a straight
        float remCm  = remaining * DEG2RAD * tn.radius;
        float slowTo = sqrtf(2.0f * STRAIGHT_DECEL *
                       fmaxf(remCm - TURN_APPROACH_CM, 0.0f)) + TURN_V_MIN;
        float upTo   = fminf(tn.v + STRAIGHT_ACCEL * 0.01f, TURN_V_MAX);
        int slowing  = (slowTo < upTo);
        tn.v = fminf(upTo, slowTo);

        // Outer wheel faster, inner slower, in the measured ratio, so they
        // roll with the steering instead of fighting it. tn.v is the average.
        float vOut = 2.0f * tn.v / (1.0f + tn.io);
        float vIn  = tn.io * vOut;
        float vL   = tn.left ? vIn  : vOut;
        float vR   = tn.left ? vOut : vIn;

        float tgtTicks  = tn.v * TICKS_PER_CM / 100.0f;
        int   brakeBoth = slowing &&
                          (0.5f * (tn.wl.filt + tn.wr.filt) > tgtTicks + TURN_ASSIST_TICKS);

        uint16_t pL = wheelControl(&tn.wl, vL, cntL, (tn.dir > 0) ? GAIN_L_FWD : GAIN_L_REV, brakeBoth);
        uint16_t pR = wheelControl(&tn.wr, vR, cntR, (tn.dir > 0) ? GAIN_R_FWD : GAIN_R_REV, brakeBoth);
        driveWheel(1, tn.dir, pL, brakeBoth);
        driveWheel(0, tn.dir, pR, brakeBoth);
        writeServo(tn.lockUs);
        break;
    }

    case TN_BRAKE:
    default:
        if (el < 200) {                                // active brake
            setMotorPwm(PWM_BRAKE, PWM_BRAKE, PWM_BRAKE, PWM_BRAKE);
            tn.stillLoops = 0;
            break;
        }
        setMotorPwm(0, 0, 0, 0);
        tn.stillLoops = (cntL == 0 && cntR == 0) ? (uint8_t)(tn.stillLoops + 1) : 0;
        if (tn.stillLoops >= 5 || el > 700) {
            tn.resAngle = turned;
            tn.resErr   = headingDeg - tn.targetAbs;
            // Start bringing the wheels back towards straight (from below, like
            // a straight's settle does) so the next straight can start sooner.
            writeServo(SERVO_TRUE_CENTRE + servoTrim - SERVO_APPROACH_US);
            tn.active = 0;
        }
        break;
    }
}

/* Turns at full lock until the heading has changed by angleDeg, then stops.
 *   left = 1 for the left lock, 0 for the right lock
 *   dir  = +1 forward, -1 reverse
 * LF: heading +,  LB: heading - (nose swings clockwise),
 * RF: heading -,  RB: heading + (nose swings anticlockwise).
 * Blocks until the robot has stopped. Results: Motion_Reply() / Motion_LogText(). */
void Motion_Turn(int left, int dir, float angleDeg)
{
    if (tn.active || mv.active) {
        setResults("BUSY", "BUSY");
        return;
    }
    if (angleDeg < 0.0f) {                             // old convention: negative = reverse
        angleDeg = -angleDeg;
        dir = -dir;
    }
    if (angleDeg < 0.5f) {
        setResults("a=0.0 e=+0.0", "a=0.0 e=+0.0");
        return;
    }

    if (!headingSet) {
        Motion_ZeroHeading();                          // no ZH received - this is "north"
    }
    emergency_stop_requested = 0;                      // a '!' from before this move doesn't count

    int cur = (int)__HAL_TIM_GET_COMPARE(&htim12, TIM_CHANNEL_2);

    tn.left      = (int8_t)(left ? 1 : 0);
    tn.dir       = (int8_t)((dir > 0) ? 1 : -1);
    tn.hsign     = (int8_t)((left ? 1 : -1) * tn.dir);
    tn.target    = angleDeg;
    tn.targetAbs = headingTarget + (float)tn.hsign * angleDeg;
    headingTarget = tn.targetAbs;                      // the plan's heading after this turn
    tn.radius    = left ? TURN_RADIUS_L : TURN_RADIUS_R;
    tn.io        = left ? TURN_IO_L : TURN_IO_R;
    tn.lockUs    = left ? SERVO_LEFT_LOCK : SERVO_RIGHT_LOCK;
    tn.settleMs  = (abs(cur - (int)tn.lockUs) > 20) ? TURN_SETTLE_MS : 60;
    tn.deadline  = HAL_GetTick() + tn.settleMs + 2000
                   + (uint32_t)(angleDeg * DEG2RAD * tn.radius * 1000.0f / TURN_V_MIN);
    tn.timedOut  = 0;
    tn.aborted   = 0;
    tn.wBrake    = 0.0f;
    tn.vBrake    = 0.0f;
    tn.phase     = TN_SETTLE;
    tn.phaseTick = HAL_GetTick();
    abortReq     = 0;
    tn.active    = 1;                                  // motor task takes over

    while (tn.active) {
        if (HAL_GPIO_ReadPin(GPIOE, GPIO_PIN_0) == GPIO_PIN_RESET) {
            abortReq = 1;                              // user button: stop now
        }
        osDelay(10);
    }

    const char *end = tn.aborted ? " ABORT" : (tn.timedOut ? " TO" : "");
    snprintf(replyShort, sizeof(replyShort), "a=%.1f e=%+.1f%s", tn.resAngle, tn.resErr, end);
    // Full diagnostics: w = yaw rate when braking began (deg/s),
    // v = wheel speed when braking began (cm/s)
    snprintf(replyLong, sizeof(replyLong), "a=%.1f e=%+.1f w=%.0f v=%.0f%s",
             tn.resAngle, tn.resErr, tn.wBrake, tn.vBrake, end);
}

/* ==========================================================================
 *  Motor task entry point
 * ========================================================================== */

/* Called by the motor task every 10 ms with the encoder ticks since the last
 * call. Runs whichever move is active; otherwise keeps the motors off. */
void Motion_Step(int16_t cntL, int16_t cntR)
{
    // The robot can't rotate if neither wheel has turned for 0.5 s.
    // The gyro task uses this to keep its zero-rate offset up to date.
    static uint16_t stillCount = 0;
    if (cntL == 0 && cntR == 0 && !mv.active && !tn.active) {
        if (stillCount < 1000) stillCount++;
    } else {
        stillCount = 0;
    }
    robotStill = (stillCount >= 50);

    if (tn.active) {
        turnStep(cntL, cntR);
        return;
    }
    if (mv.active) {
        straightStep(cntL, cntR);
        return;
    }

    // Idle: motors off. A '!' while idle also straightens the wheels.
    setMotorPwm(0, 0, 0, 0);
    if (emergency_stop_requested) {
        writeServo(SERVO_TRUE_CENTRE);
        emergency_stop_requested = 0;
    }
}

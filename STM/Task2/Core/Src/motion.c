/*
 * motion.c
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
#include "FreeRTOS.h"
#include "task.h"                               // taskENTER_CRITICAL for the pose
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "robot_config.h"
#include "motion.h"

#ifndef T2B_WALL_MISS_N
#define T2B_WALL_MISS_N 3       // watchSide (Banzai): odd readings in a row ignored
#endif

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
    uint16_t endSteerUs;          // Step 9: servo value to write when braking starts (next turn's lock), 0 = none
    uint8_t  flyIn, flyOut;       // Banzai: start at the handed-over speed / end at speed without braking
    float    vExit;               // flyOut: speed to hand over (cm/s)
    float    brakeAt;             // Banzai entry / VB: no slow-down, hard brake this close to the target (cm); 0 = off
    uint8_t  vmaxTest;            // VMAX? running
    char     vTrip;               // VMAX?: why the ramp stopped (P / L / Y / D), 0 = still ramping
    float    vRamp;               // VMAX?: ramp target (cm/s)
    uint8_t  satCount, lagCount;  // VMAX?: loops in a row saturated / lagging
    float    tripV, tripVL, tripVR, tripErr, tripServo, tripDist;   // VMAX?: state when it stopped
    uint16_t tripPL, tripPR;
    float    wRef, wStart, wSum, wBest, wBestMean;   // watchSide: current steady stretch / the longest one
    uint16_t wN;
    uint8_t  wRun, wMiss;
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
    /* Sensor-ended straights (SU / SW / SE / carpark entry). until = 0 on a plain straight. */
    float    vCap;                // Cruise speed cap (cm/s); STRAIGHT_V_MAX on a plain straight
    uint8_t  until;               // 1 = the sensors set the target every loop (options in opt)
    Until_t  opt;
    uint32_t usSeq;               // Last echo already used
    float    est[3];              // Last 3 ultrasonic stop points (cm of travel)
    uint8_t  nEst;                // Good echoes used so far (saturates at 255)
    float    usAt;                // Median ultrasonic stop point (cm of travel), valid once nEst >= 3
    uint32_t irSeqSeen;           // Last IR update already used
    uint8_t  faceCount, edgeCount, guardCount, emergCount;   // readings in a row
    uint8_t  faceSeen, edgeFound, guardHit, emergHit;
    uint8_t  edgeFar;             // readings since the edge candidate began that were >= IR_EDGE_FAR_CM
    uint8_t  backCount;           // after the edge: readings in a row back on a face
    uint8_t  edgeRej;             // Diagnostic: edge candidates ignored (too early) or dropped (still the face)
    float    faceAvg;             // Typical reading on the face (cm)
    float    edgeDist;            // Travel at the first reading past the edge
    float    edgeV;               // Speed then (cm/s): the detection lag grows with speed
    float    edgeAt;              // Stop point from the IR edge (cm of travel)
    char     stopBy;              // U ultrasonic, E IR edge, C cap, F floor, G guard / emergency
    uint8_t  slowSeen;            // Diagnostic: slowing for the stop has begun
    float    eSlow, ySlow;        // Diagnostic: heading error / sideways drift when slowing began
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
    uint16_t endSteerUs;          // Step 9: servo value to leave at the end (next turn's lock), 0 = centre
    uint8_t  flyIn, flyOut;       // Banzai: start at the handed-over speed / end at speed without braking
    float    vExit;               // flyOut: speed to hand over (cm/s)
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
    uint16_t lastPL, lastPR;      // Banzai: last wheel PWM (held through a flying hand-over)
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
static MoveResult_t lastRes = {0};      // for the Task 2 sequencer
static char replyLong[92]  = "";     // fits the 96-byte reply buffer in main.c with "A " and "\n"

static void setResults(const char *shortText, const char *longText)
{
    // Only used for moves that end before they start (BUSY or zero length)
    lastRes.end = (shortText[0] == 'B') ? MOVE_BUSY : MOVE_OK;
    lastRes.dist = 0.0f;
    lastRes.u = -1.0f;
    lastRes.stopBy = 'C';
    lastRes.faceSeen = 0;
    lastRes.target = 0.0f;
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

void Motion_LastResult(MoveResult_t *r)
{
    *r = lastRes;
}

static void setLastEnd(uint8_t aborted, uint8_t timedOut)
{
    lastRes.end = aborted ? MOVE_ABORT : (timedOut ? MOVE_TIMEOUT : MOVE_OK);
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

/* ---- Step 9: steer for the next turn while this move stops ------------------
 * The sequencer calls Motion_EndSteer() just before a move that a turn will
 * follow. The move takes the request when it starts (any move: a skipped or
 * different move simply drops it) and, as it stops, writes that lock. The
 * turn then only waits for what is left of the swing. */
static int8_t   endSteerNext  = -1;   // requested by Motion_EndSteer, taken by the next move
static uint16_t preSteerUs    = 0;    // lock written early by the last move, 0 = none
static uint16_t preSteerSwing = 0;    // ... how far the servo had to swing (us)
static uint32_t preSteerTick  = 0;    // ... when

void Motion_EndSteer(int lock)
{
    endSteerNext = (int8_t)((lock < 0) ? -1 : (lock ? 1 : 0));
}

static uint16_t takeEndSteer(void)
{
    int8_t l = endSteerNext;
    endSteerNext = -1;
#if FLOW_MOVES
    return (l < 0) ? 0 : (uint16_t)(l ? SERVO_LEFT_LOCK : SERVO_RIGHT_LOCK);
#else
    (void)l;
    return 0;
#endif
}

/* Servo settle for a swing of this many us (the turn rule of QUICK_STOPS) */
static uint16_t swingSettleMs(int swing)
{
#if TURN_SETTLE_PROP
    uint32_t ms = (swing > 20) ? 60u + (uint32_t)(TURN_SETTLE_MS - 60) * (uint32_t)swing / (SERVO_RIGHT_LOCK - SERVO_LEFT_LOCK) : 60u;
    return (uint16_t)((ms > TURN_SETTLE_MS) ? TURN_SETTLE_MS : ms);
#else
    return (swing > 20) ? TURN_SETTLE_MS : 60;
#endif
}

static void preSteer(uint16_t us)
{
    int cur = (int)__HAL_TIM_GET_COMPARE(&htim12, TIM_CHANNEL_2);
    preSteerSwing = (uint16_t)abs(cur - (int)us);
    preSteerTick  = HAL_GetTick();
    preSteerUs    = us;
    writeServo(us);
}

/* ---- Banzai: flying hand-over between moves ---------------------------------
 * Motion_Fly() before a move: flyIn = start at the speed the last move handed
 * over (no stop, no servo settle: the servo swings while rolling); flyOut =
 * end at T2B_FLY_V without braking. While the command task sets up the next
 * move (up to ~20 ms) the motors keep the last PWM (flyHold). If the last move
 * stopped normally, flyIn does nothing. Nothing here runs unless the sequencer
 * asks for it (BZ_FLY). */
static uint8_t  flyNext   = 0;        // bit 0 = flyIn, bit 1 = flyOut, for the next move
static float    exitV     = 0.0f;     // speed handed over by the last move (cm/s), 0 = it stopped
static int8_t   exitDir   = 1;
static WheelCtl_t exitWL, exitWR;     // its wheel controllers, so the next move starts smoothly
static uint16_t holdPL = 0, holdPR = 0;
static uint32_t flyHoldUntil = 0;     // motor task: keep holdPL / holdPR until this tick
static uint32_t flyBrakeUntil = 0;    // motor task: a hand-over nobody took: brake until this tick

void Motion_Fly(int flyIn, int flyOut)
{
    flyNext = (uint8_t)((flyIn ? 1 : 0) | (flyOut ? 2 : 0));
}

static uint8_t takeFly(void)
{
    uint8_t f = flyNext;
    flyNext = 0;
#ifdef T2B_FLY_V
    return f;
#else
    (void)f;
    return 0;
#endif
}

static void handOver(float v, int8_t dir, const WheelCtl_t *l, const WheelCtl_t *r, uint16_t pL, uint16_t pR)
{
    exitV = v;
    exitDir = dir;
    exitWL = *l;
    exitWR = *r;
    holdPL = pL;
    holdPR = pR;
    flyHoldUntil = HAL_GetTick() + 40;
}

/* ==========================================================================
 *  Heading
 * ========================================================================== */

/* ==========================================================================
 *  Tracked pose (rear-axle centre). See robot_config.h section 9 for the frame.
 *  Written only by the motor task (poseStep), read by any task via
 *  Motion_GetPose(). The x, y pair is copied inside a critical section so a
 *  reader never gets x from one 10 ms step and y from the next.
 * ========================================================================== */
static float poseX = POSE_START_X_CM;
static float poseY = 0.0f;
static float poseHPrev = 0.0f;           // heading at the previous step (deg, relative to ZH)

static void poseReset(void)
{
    taskENTER_CRITICAL();
    poseX = POSE_START_X_CM;
    poseY = 0.0f;
    poseHPrev = 0.0f;
    taskEXIT_CRITICAL();
}

/* One 10 ms step: move along the average heading of this step by the average
 * rear-wheel travel. Exact on straights and on arcs (rear-axle centre). */
static void poseStep(int16_t cntL, int16_t cntR)
{
    float h  = headingDeg - headingZero;
    float ds = 0.5f * (float)(cntL + cntR) / TICKS_PER_CM;     // signed, + = forward
#if POSE_TURN_FROM_RADIUS
    if (tn.active) {
        // In a turn the rear wheels scrub, so their average under-reads the arc
        // (LF090 read R = 18.1, not 20.2). Use the calibrated radius x the
        // gyro's turn instead; the radius came from measured LF180 / RF180.
        float dsArc = (float)tn.dir * tn.radius * fabsf(h - poseHPrev) * DEG2RAD;
        // Banzai flown turn (14 Oct): while the servo is still swinging to the lock
        // the robot rolls almost straight, which gyro x radius misses: take the
        // larger. Standing turns (step 9) are unchanged.
        if (tn.flyIn && fabsf(ds) > fabsf(dsArc)) dsArc = ds;
        ds = dsArc;
    }
#endif
    float hm = 0.5f * (h + poseHPrev) * DEG2RAD;
    float dx = ds * cosf(hm);
    float dy = ds * sinf(hm);
    if (POSE_SLIDE_CM_PER_M > 0.0f && mv.active && mv.dir > 0 && ds > 0.0f) {
        float slide = POSE_SLIDE_CM_PER_M * 0.01f * ds;          // to the right of travel
        dx += slide * sinf(hm);
        dy -= slide * cosf(hm);
    }
    taskENTER_CRITICAL();
    poseX += dx;
    poseY += dy;
    taskEXIT_CRITICAL();
    poseHPrev = h;
}

/* Current tracked pose: x, y in cm (carpark frame), h in degrees relative
 * to ZH (CCW +). Any task; never waits. Pass NULL for values not needed. */
void Motion_GetPose(float *x, float *y, float *h)
{
    float px, py;
    taskENTER_CRITICAL();
    px = poseX;
    py = poseY;
    taskEXIT_CRITICAL();
    if (x) *x = px;
    if (y) *y = py;
    if (h) *h = headingDeg - headingZero;
}

/* "ZH": make the robot's current direction "north" for this run, and put the
 * tracked pose back at the marked start (POSE_START_X_CM, 0). */
void Motion_ZeroHeading(void)
{
    headingZero   = headingDeg;
    headingTarget = headingDeg;
    headingSet    = 1;
    poseReset();
}

/* ==========================================================================
 *  Straights
 * ========================================================================== */

static float median3(float a, float b, float c)
{
    float lo = fminf(a, b), hi = fmaxf(a, b);
    return fmaxf(lo, fminf(hi, c));
}

static uint8_t inc8(uint8_t n)
{
    return (n < 200) ? (uint8_t)(n + 1) : n;
}

/* Sensor straights only (mv.until). Called every 10 ms from straightStep in
 * the motor task; never waits. Turns the sensors into a stop point (cm of
 * travel) and writes it to mv.target; the normal profile then stops there.
 *  - Ultrasonic: each echo -> stop point; median of the last 3, so one false
 *    echo can't move it.
 *  - IR edge: 3 readings in a row under IR_FACE_CM = a face; then 2 readings
 *    in a row more than IR_EDGE_JUMP_CM above it, 2 of them IR_EDGE_FAR_CM or
 *    more, = the edge; stop edgeExtra after where the edge was first seen.
 *    Jumps before edgeFromCm of travel are ignored; a jump that stays under
 *    IR_EDGE_FAR_CM is still the face (the face reading follows it). If the
 *    face comes back within IR_EDGE_CONFIRM_CM while still driving, that edge
 *    was a seam or the card's edge: dropped, the search goes on.
 *  - Guard IR / ultrasonic emergency: 2 readings in a row -> stop now. */
static void untilTrack(float dist, float vNow)
{
    float r;
    uint32_t seq = Ultra_Get(&r);
    if (seq != mv.usSeq) {                               // a new echo
        mv.usSeq = seq;
        if (r > 0.0f && mv.opt.usEmergCm > 0.0f) {
            mv.emergCount = (r < mv.opt.usEmergCm) ? inc8(mv.emergCount) : 0;
            if (mv.emergCount >= 2) mv.emergHit = 1;
        }
        if (r > 0.0f && mv.opt.usStop > 0.0f &&
            !(r < mv.opt.ignBelow && dist < mv.opt.ignUntil)) {   // e.g. the carpark arms at the start
            mv.est[0] = mv.est[1];
            mv.est[1] = mv.est[2];
            mv.est[2] = dist + (r - mv.opt.usStop) - vNow * ULTRA_LAG_S;
            mv.nEst = inc8(mv.nEst);
            if (mv.nEst >= 3) mv.usAt = median3(mv.est[0], mv.est[1], mv.est[2]);
        }
    }

    if (irSeq != mv.irSeqSeen) {                         // a new IR update
        mv.irSeqSeen = irSeq;
        if (mv.opt.edgeSide >= 0 && !mv.edgeFound) {
            float d = irCm[mv.opt.edgeSide];
            if (!mv.faceSeen) {
                mv.faceCount = (d < IR_FACE_CM) ? inc8(mv.faceCount) : 0;
                if (mv.faceCount >= 3) {
                    mv.faceSeen = 1;
                    mv.faceAvg  = d;
                }
            } else if (d > mv.faceAvg + IR_EDGE_JUMP_CM) {   // past the end of the face, or a patch on it
                if (dist < mv.opt.edgeFromCm) {          // too early to be the end (image card): ignore
                    if (mv.edgeCount == 0) mv.edgeRej = inc8(mv.edgeRej);
                    mv.edgeCount = 0;
                    mv.edgeFar   = 0;
                } else {
                    if (mv.edgeCount == 0) {
                        mv.edgeDist = dist;
                        mv.edgeV    = vNow;
                        mv.edgeFar  = 0;
                    }
                    mv.edgeCount = inc8(mv.edgeCount);
                    if (d >= IR_EDGE_FAR_CM) mv.edgeFar = inc8(mv.edgeFar);
                    if (mv.edgeCount >= 2 && mv.edgeFar >= 2) {
                        mv.edgeFound = 1;
                        // IR_EDGE_LAG_CM was measured at FACE_V_MAX; the lag is mostly time, so scale it
                        mv.edgeAt = mv.edgeDist - IR_EDGE_LAG_CM * (mv.edgeV / FACE_V_MAX) + mv.opt.edgeExtra;
                    } else if (mv.edgeCount >= IR_EDGE_HOLD_N && mv.edgeFar == 0) {
                        mv.edgeRej   = inc8(mv.edgeRej);     // stayed near: the face just reads higher here
                        mv.edgeCount = 0;
                        mv.faceAvg   = d;
                    }
                }
            } else {                                     // still on the face
                mv.edgeCount = 0;
                mv.faceAvg = 0.8f * mv.faceAvg + 0.2f * d;
            }
        } else if (mv.opt.edgeSide >= 0 && dist < mv.edgeDist + IR_EDGE_CONFIRM_CM) {
            // Edge found, still near it: back on a face = it was not the end
            mv.backCount = (irCm[mv.opt.edgeSide] < IR_FACE_CM) ? inc8(mv.backCount) : 0;
            if (mv.backCount >= 2) {
                mv.edgeFound = 0;
                mv.edgeCount = mv.edgeFar = mv.backCount = 0;
                mv.edgeRej   = inc8(mv.edgeRej);
            }
        }
        if (mv.opt.watchSide >= 0) {                     // Banzai: longest steady stretch of a side IR
            float d = irCm[mv.opt.watchSide];
            if (mv.wRun && d < IR_CLEAR && fabsf(d - mv.wRef) <= mv.opt.watchTol) {
                mv.wSum += d;
                mv.wN++;
                mv.wRef  = mv.wSum / (float)mv.wN;       // compare with the stretch's mean so far
                mv.wMiss = 0;
                if (dist - mv.wStart > mv.wBest) {
                    mv.wBest     = dist - mv.wStart;
                    mv.wBestMean = mv.wRef;
                }
            } else if (mv.wRun && mv.wMiss < T2B_WALL_MISS_N) {
                mv.wMiss++;                              // a few odd readings: keep the stretch
            } else if (d < IR_CLEAR) {
                mv.wRef = d; mv.wStart = dist; mv.wSum = d; mv.wN = 1; mv.wRun = 1; mv.wMiss = 0;
            } else {
                mv.wRun = 0;
            }
        }
        if (mv.opt.guardSide >= 0 && dist >= mv.opt.guardFromCm) {
            mv.guardCount = (irCm[mv.opt.guardSide] < mv.opt.guardCm) ? inc8(mv.guardCount) : 0;
            if (mv.guardCount >= 2) mv.guardHit = 1;
        }
    }

    // Combine: IR edge (or the cap), then the ultrasonic, then the floor
    float tgt;
    char  by;
    if (mv.edgeFound)      { tgt = mv.edgeAt;        by = 'E'; }
    else if (mv.faceSeen)  { tgt = mv.opt.edgeMaxCm; by = 'C'; }
    else                   { tgt = mv.opt.maxCm;     by = 'C'; }
    if (mv.nEst >= 3 && mv.usAt < tgt) { tgt = mv.usAt; by = 'U'; }
    if (tgt < mv.opt.minCm) {
        float rNow = mv.usAt - dist + mv.opt.usStop;     // what the ultrasonic reads now
        if (!(by == 'U' && rNow <= ULTRA_HARD_STOP_CM)) {  // a real close echo still stops it
            tgt = mv.opt.minCm;
            by  = 'F';
        }
    }
    if (mv.guardHit || mv.emergHit) {                    // something too close: stop now
        tgt = fminf(tgt, dist);
        by  = 'G';
    }
    mv.target = tgt;
    mv.stopBy = by;
}

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
        if (!mv.flyIn) setMotorPwm(0, 0, 0, 0);
        if (mv.until) untilTrack(0.0f, 0.0f);           // standing still: readings count from 0 cm
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
            mv.slowSeen = 0;
            mv.eSlow = mv.ySlow = 0.0f;
            mv.servoCmd = SERVO_TRUE_CENTRE + servoTrim;
            // Settled from below, so the wheels sit at the low side of the play
            mv.wheelEst = mv.servoCmd - 0.5f * STEER_PLAY_US;
            mv.v = STRAIGHT_V_MIN;
            mv.wl.integ = mv.wr.integ = 0.0f;
            mv.wl.filt  = mv.wr.filt  = 0.0f;
            mv.wl.still = mv.wr.still = KICK_AFTER_LOOPS;   // burst from the first loop
            if (mv.flyIn) {                            // Banzai: carry on at the handed-over speed
                mv.v  = fmaxf(STRAIGHT_V_MIN, exitV);
                mv.wl = exitWL;
                mv.wr = exitWR;
                mv.wl.still = mv.wr.still = 0;
            }
            exitV = 0.0f;
            mv.phase = MV_DRIVE;
            mv.phaseTick = now;
        }
        break;

    case MV_DRIVE: {
        float vNow = 0.5f * (mv.wl.filt + mv.wr.filt) * 100.0f / TICKS_PER_CM;
        if (mv.until) untilTrack(dist, vNow);           // sensor straights: the sensors move the target
        float remaining = mv.target - dist;
        // Brake early enough for the speed we are actually doing: the robot
        // keeps rolling for ~STOP_T seconds after the decision, so the stop
        // point moves back in proportion to speed. Capped at the approach zone
        // so it never brakes early while still slowing down from cruise.
        float stopDist = fminf(STOP_T * vNow, STRAIGHT_APPROACH);
        if (mv.brakeAt > 0.0f) stopDist = mv.brakeAt;  // Banzai entry / VB: hard brake at a fixed distance
        if (mv.flyOut && remaining <= 0.0f && !mv.timedOut) {   // Banzai: hand over at speed, no brake
            if (mv.endSteerUs) preSteer(mv.endSteerUs);  // the next turn's lock, now
            mv.resDist = dist;
            mv.resHead = headingDeg - (float)mv.heading0;
            handOver(vNow, mv.dir, &mv.wl, &mv.wr,
                     (uint16_t)__HAL_TIM_GET_COMPARE(&htim4, (mv.dir > 0) ? TIM_CHANNEL_4 : TIM_CHANNEL_3),
                     (uint16_t)__HAL_TIM_GET_COMPARE(&htim9, (mv.dir > 0) ? TIM_CHANNEL_2 : TIM_CHANNEL_1));
            mv.active = 0;
            break;
        }
        if ((!mv.flyOut && remaining <= stopDist) || now > mv.deadline) {
            mv.timedOut = (remaining > stopDist);
            mv.vBrake = vNow;
            mv.eBrake = headingDeg - (float)mv.heading0;
            mv.phase = MV_BRAKE;
            mv.phaseTick = now;
            setMotorPwm(PWM_BRAKE, PWM_BRAKE, PWM_BRAKE, PWM_BRAKE);
            if (mv.endSteerUs) preSteer(mv.endSteerUs);  // step 9: the next turn's lock, while stopping
            break;
        }

        // Speed profile: speed up, cruise, slow down so we arrive at V_MIN
        float slowTo = sqrtf(2.0f * STRAIGHT_DECEL *
                       fmaxf(remaining - STRAIGHT_APPROACH, 0.0f)) + STRAIGHT_V_MIN;
        if (mv.flyOut) slowTo = sqrtf(2.0f * STRAIGHT_DECEL * fmaxf(remaining, 0.0f)) + mv.vExit;
        if (mv.brakeAt > 0.0f) slowTo = 1.0e6f;        // full speed up to the brake point
        float vTop   = mv.vCap;
        if (mv.vmaxTest && !mv.vTrip) {                // VMAX?: slow ramp
            mv.vRamp = fminf(mv.vRamp + VMAX_RAMP * 0.01f, VMAX_V_CAP);
            vTop = fminf(vTop, mv.vRamp);
        }
        if (mv.until && mv.opt.usStop > 0.0f && mv.opt.edgeSide < 0 && mv.nEst < 3) {
            vTop = fminf(vTop, ULTRA_BLIND_V);         // ultrasonic stop, nothing seen yet: go easy
        }
        if (mv.until && mv.opt.slowZoneCm > 0.0f) {    // arrive at the slow zone at vSlow, stay there
            float zone = remaining - mv.opt.slowZoneCm;
            vTop = fminf(vTop, (zone <= 0.0f) ? mv.opt.vSlow
                                              : sqrtf(2.0f * STRAIGHT_DECEL * zone) + mv.opt.vSlow);
        }
        float upTo   = fminf(mv.v + STRAIGHT_ACCEL * 0.01f, vTop);
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
        if (slowing && !mv.slowSeen) {                 // diagnostic: drift so far, before the stop
            mv.slowSeen = 1;
            mv.eSlow = err;
            mv.ySlow = mv.lateral;
        }
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

        if (mv.vmaxTest && !mv.vTrip) {                // VMAX?: has a wheel run out of power?
            float vL = mv.wl.filt * 100.0f / TICKS_PER_CM;
            float vR = mv.wr.filt * 100.0f / TICKS_PER_CM;
            mv.satCount = (pL >= (uint16_t)MOTOR_PWM_MAX || pR >= (uint16_t)MOTOR_PWM_MAX) ? (uint8_t)(mv.satCount + 1) : 0;
            mv.lagCount = (mv.v > 30.0f && (mv.v - vL > VMAX_LAG_CMS || mv.v - vR > VMAX_LAG_CMS))
                          ? (uint8_t)(mv.lagCount + 1) : 0;
            char why = 0;
            float stopNeed = vNow * vNow / (2.0f * STRAIGHT_DECEL) + STRAIGHT_APPROACH + 2.0f;
            if (mv.satCount >= VMAX_SAT_LOOPS)       why = 'P';
            else if (mv.lagCount >= VMAX_LAG_LOOPS)  why = 'L';
            else if (fabsf(err) > VMAX_YAW_DEG)      why = 'Y';
            else if (remaining <= stopNeed)          why = 'D';
            if (why) {
                mv.vTrip = why;
                mv.tripV = vNow; mv.tripVL = vL; mv.tripVR = vR; mv.tripErr = err;
                mv.tripPL = pL; mv.tripPR = pR; mv.tripDist = dist;
                mv.tripServo = mv.servoCmd - (SERVO_TRUE_CENTRE + servoTrim);
                mv.target = fminf(mv.target, dist + stopNeed);   // slow down and stop from here
            }
        }
        break;
    }

    case MV_BRAKE:
    default:
        if (el < MOVE_BRAKE_MS ||
            (mv.brakeAt > 0.0f && (cntL != 0 || cntR != 0) && el < 600)) {   // active brake (hard brake: until stopped)
            setMotorPwm(PWM_BRAKE, PWM_BRAKE, PWM_BRAKE, PWM_BRAKE);
            mv.stillLoops = 0;
            break;
        }
        setMotorPwm(0, 0, 0, 0);
        mv.stillLoops = (cntL == 0 && cntR == 0) ? (uint8_t)(mv.stillLoops + 1) : 0;
        if (mv.stillLoops >= MOVE_STILL_LOOPS || el > 700) {
            mv.resDist = dist;
            mv.resHead = headingDeg - (float)mv.heading0;   // final error vs target
            exitV = 0.0f;                              // stopped: nothing to hand over
            mv.active = 0;
        }
        break;
    }
}

/* Sets up a straight and blocks until the robot has stopped. The caller has
 * already checked Motion_IsBusy() and set mv.vCap / mv.until (and, for
 * sensor straights, mv.opt and the tracking state). Writes the plain "d= e=" replies. */
static void straightRun(float distance_cm)
{
    if (!headingSet) {
        Motion_ZeroHeading();                          // no ZH received - this is "north"
    }
    emergency_stop_requested = 0;                      // a '!' from before this move doesn't count
    preSteerUs = 0;                                    // this straight steers for itself

    // Short servo settle if it is already near centre (including the
    // just-below-centre position a turn leaves it at), longer otherwise
    int curServo = (int)__HAL_TIM_GET_COMPARE(&htim12, TIM_CHANNEL_2);
    int ctr = (int)(SERVO_TRUE_CENTRE + servoTrim);
    int far = (curServo < ctr - SERVO_APPROACH_US - 5) || (curServo > ctr + 20);

    mv.dir       = (distance_cm > 0.0f) ? 1 : -1;
    mv.target    = fabsf(distance_cm);
    mv.settleA   = far ? 250 : SETTLE_NEAR_MS;
    mv.settleB   = far ? 200 : SETTLE_NEAR_MS;
    if (mv.flyIn && exitV > 0.0f && exitDir == ((distance_cm > 0.0f) ? 1 : -1)) {
        mv.settleA = mv.settleB = 0;                   // Banzai: already rolling, no settle
    } else {
        mv.flyIn = 0;
    }
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
    // es / ys = heading error and sideways drift when slowing for the stop began
    snprintf(replyLong, sizeof(replyLong), "d=%.1f e=%+.1f eb=%+.1f y=%+.1f m=%+.1f s=%+d vb=%.0f t=%+.0f es=%+.1f ys=%+.1f%s",
             mv.resDist, mv.resHead, mv.eBrake, mv.lateral, mv.eEarly,
             (int)mv.startLoopR - (int)mv.startLoopL,
             mv.vBrake, servoTrim, mv.eSlow, mv.ySlow, end);
    setLastEnd(mv.aborted, mv.timedOut);
    lastRes.dist = mv.resDist;
    lastRes.u = -1.0f;
    lastRes.stopBy = 'D';
    lastRes.faceSeen = 0;
    lastRes.target = mv.target;
}

/* Drives straight by distance_cm (negative = backwards) and blocks until the
 * robot has stopped. Results: Motion_Reply() / Motion_LogText(). */
void Motion_Straight(float distance_cm)
{
    mv.endSteerUs = takeEndSteer();                    // step 9 (dropped if this move doesn't run)
    uint8_t fly   = takeFly();                         // Banzai (dropped likewise)
    if (Motion_IsBusy()) {
        setResults("BUSY", "BUSY");
        return;
    }
    if (fabsf(distance_cm) < 0.5f) {
        setResults("d=0.0 e=+0.0", "d=0.0 e=+0.0");
        return;
    }
    mv.vCap  = STRAIGHT_V_MAX;
    mv.until = 0;                                      // plain straight: no sensors
    mv.flyIn = fly & 1; mv.flyOut = (fly >> 1) & 1;
#ifdef T2B_FLY_V
    mv.vExit = T2B_FLY_V;
#endif
    mv.brakeAt = 0.0f;
    mv.vmaxTest = 0;
    straightRun(distance_cm);
    lastRes.watchSpan = lastRes.watchMean = 0.0f;
}

void Motion_UntilInit(Until_t *o)
{
    memset(o, 0, sizeof(*o));
    o->vCap      = STRAIGHT_V_MAX;
    o->edgeSide  = -1;
    o->guardSide = -1;
    o->vSlow     = STRAIGHT_V_MIN;
    o->watchSide = -1;
}

/* Straight (forward, or backward with o->reverse) whose stop point comes from the sensors (see Until_t and
 * untilTrack). Blocks until stopped.
 * Reply: "d=<travel> e=<heading err> u=<ultrasonic at the stop> by=<U|E|C|F|G>"
 *   u  = predicted from the tracked echoes and the encoders, -1 if fewer
 *        than 3 good echoes (or no ultrasonic stop)
 *   by = U ultrasonic, E IR edge, C cap, F floor overruled a short echo,
 *        G guard IR / ultrasonic emergency */
void Motion_StraightOpts(const Until_t *o)
{
    mv.endSteerUs = takeEndSteer();                    // step 9 (dropped if this move doesn't run)
    uint8_t fly   = takeFly();                         // Banzai (dropped likewise)
    if (Motion_IsBusy()) {
        setResults("BUSY", "BUSY");
        return;
    }
    float far = fmaxf(o->maxCm, o->edgeSide >= 0 ? o->edgeMaxCm : 0.0f);
    if (far < 0.5f) {
        setResults("d=0.0 e=+0.0 u=-1.0 by=C", "d=0.0 e=+0.0 u=-1.0 by=C");
        return;
    }
    float dummy;
    mv.opt = *o;
    if (mv.opt.edgeMaxCm < mv.opt.maxCm) mv.opt.edgeMaxCm = mv.opt.maxCm;
    mv.opt.minCm = fminf(fmaxf(o->minCm, 0.0f), far);
    mv.vCap      = fminf(o->vCap, STRAIGHT_V_ABS_MAX);
    mv.usSeq     = Ultra_Get(&dummy);                  // only readings from now on count
    mv.irSeqSeen = irSeq;
    mv.nEst = 0;
    mv.usAt = 0.0f;
    mv.faceCount = mv.edgeCount = mv.guardCount = mv.emergCount = 0;
    mv.faceSeen = mv.edgeFound = mv.guardHit = mv.emergHit = 0;
    mv.edgeFar = mv.edgeRej = mv.backCount = 0;
    mv.faceAvg = mv.edgeDist = mv.edgeAt = mv.edgeV = 0.0f;
    if (mv.opt.reverse) {
        mv.opt.usStop = 0.0f;                          // the ultrasonic looks forward
        mv.opt.usEmergCm = 0.0f;
    }
    mv.wRun = mv.wMiss = 0; mv.wBest = mv.wBestMean = 0.0f; mv.wN = 0;
    mv.flyIn  = fly & 1; mv.flyOut = (fly >> 1) & 1;
#ifdef T2B_FLY_V
    mv.vExit  = T2B_FLY_V;
#endif
    mv.brakeAt  = mv.opt.brakeAtCm;
    mv.vmaxTest = 0;
    mv.stopBy = 'C';
    mv.until  = 1;
    straightRun(mv.opt.reverse ? -far : far);          // the sensors set the real target each loop
    mv.until  = 0;
    mv.brakeAt = 0.0f;

    float u = (mv.opt.usStop > 0.0f && mv.nEst >= 3) ? (mv.usAt - mv.resDist + mv.opt.usStop) : -1.0f;
    const char *end = mv.aborted ? " ABORT" : (mv.timedOut ? " TO" : "");
    snprintf(replyShort, sizeof(replyShort), "d=%.1f e=%+.1f u=%.1f by=%c%s",
             mv.resDist, mv.resHead, u, mv.stopBy, end);
    // Full: y = sideways drift (cm), n = echoes used, f = edge IR saw a face, er = edge candidates rejected,
    //       vb = speed when braking began
    snprintf(replyLong, sizeof(replyLong), "d=%.1f e=%+.1f eb=%+.1f u=%.1f by=%c y=%+.1f n=%u f=%u er=%u vb=%.0f t=%+.0f es=%+.1f ys=%+.1f%s",
             mv.resDist, mv.resHead, mv.eBrake, u, mv.stopBy, mv.lateral, (unsigned)mv.nEst,
             (unsigned)mv.faceSeen, (unsigned)mv.edgeRej, mv.vBrake, servoTrim, mv.eSlow, mv.ySlow, end);
    lastRes.u = u;
    lastRes.stopBy = mv.stopBy;
    lastRes.faceSeen = mv.faceSeen;
    lastRes.target = mv.target;
    lastRes.watchSpan = mv.wBest;
    lastRes.watchMean = mv.wBestMean;
}

/* VMAX?: straight on a long clear floor. The target speed ramps up slowly
 * (VMAX_RAMP) with the normal wheel and heading control, until a wheel sits
 * at MOTOR_PWM_MAX (P), a wheel falls behind the target (L), the heading can't
 * be held (Y), or the floor runs out (D). Then it slows down and stops.
 * Reply: "VM v=<cm/s at the trip> by=<P|L|Y|D> vl= vr= (wheel speeds) pl= pr=
 * (wheel PWM) e=<heading err> s=<steering offset us> d=<cm at the trip>". */
void Motion_VmaxTest(float maxCm, char *reply, unsigned len)
{
    flyNext = 0;
    mv.endSteerUs = 0;
    if (Motion_IsBusy()) {
        snprintf(reply, len, "VM BUSY");
        return;
    }
    mv.vCap = VMAX_V_CAP;                              // no STRAIGHT_V_ABS_MAX here: that is what we measure
    mv.until = 0;
    mv.flyIn = mv.flyOut = 0;
    mv.brakeAt = 0.0f;
    mv.vmaxTest = 1;
    mv.vTrip = 0;
    mv.vRamp = 30.0f;
    mv.satCount = mv.lagCount = 0;
    mv.tripV = mv.tripVL = mv.tripVR = mv.tripErr = mv.tripServo = mv.tripDist = 0.0f;
    mv.tripPL = mv.tripPR = 0;
    straightRun((maxCm > 50.0f) ? maxCm : VMAX_TEST_CM);
    mv.vmaxTest = 0;
    snprintf(reply, len, "VM v=%.0f by=%c vl=%.0f vr=%.0f pl=%u pr=%u e=%+.1f s=%+.0f d=%.0f%s",
             mv.tripV, mv.vTrip ? mv.vTrip : '-', mv.tripVL, mv.tripVR, (unsigned)mv.tripPL, (unsigned)mv.tripPR,
             mv.tripErr, mv.tripServo, mv.tripDist, mv.aborted ? " ABORT" : "");
}

/* VB<v>: 100 cm at v cm/s, then the Banzai hard brake exactly at 100 cm (no
 * slow-down). Tape where the robot stops: skid = stop - 100 cm. Use that as
 * T2B_BRAKE_CM for T2B_ENTRY_V = v. Reply: the usual "d= ..." (encoder travel,
 * which misses any sliding). */
void Motion_BrakeTest(float v)
{
    Until_t o;
    Motion_UntilInit(&o);
    o.maxCm     = 100.0f;
    o.vCap      = (v > 15.0f) ? v : 15.0f;
    o.brakeAtCm = 0.1f;
    Motion_StraightOpts(&o);
}

/* SU / SW (see motion.h). */
void Motion_StraightUntil(float usStopCm, float minCm, float maxCm, float vCap,
                          float ignBelowCm, float ignUntilCm)
{
    Until_t o;
    Motion_UntilInit(&o);
    o.usStop   = usStopCm;
    o.minCm    = minCm;
    o.maxCm    = maxCm;
    o.vCap     = vCap;
    o.ignBelow = ignBelowCm;
    o.ignUntil = ignUntilCm;
    if (usStopCm < 1.0f) {
        setResults("d=0.0 e=+0.0 u=-1.0 by=C", "d=0.0 e=+0.0 u=-1.0 by=C");
        return;
    }
    Motion_StraightOpts(&o);
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
        if (!tn.flyIn) setMotorPwm(0, 0, 0, 0);
        writeServo(tn.lockUs);
        if (el >= tn.settleMs) {
            tn.heading0 = headingDeg;
            tn.v = TURN_V_MIN;
            tn.wl.integ = tn.wr.integ = 0.0f;
            tn.wl.filt  = tn.wr.filt  = 0.0f;
            tn.wl.still = tn.wr.still = KICK_AFTER_LOOPS;   // start burst from the first loop
            if (tn.flyIn) {                            // Banzai: carry on at the handed-over speed
                tn.v  = fmaxf(TURN_V_MIN, exitV);
                tn.wl = exitWL;
                tn.wr = exitWR;
                tn.wl.still = tn.wr.still = 0;
            }
            exitV = 0.0f;
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
#ifdef T2B_FLY_LEAD_S
        if (tn.flyOut && remaining <= wNow * T2B_FLY_LEAD_S && now <= tn.deadline) {
            // Banzai: hand over at speed. The wheels swing back to straight while
            // the robot rolls on, so let go this much early; the next straight's
            // heading hold takes out what is left.
            writeServo(SERVO_TRUE_CENTRE + servoTrim - SERVO_APPROACH_US);
            preSteerUs  = 0;
            tn.resAngle = turned;
            tn.resErr   = headingDeg - tn.targetAbs;
            tn.wBrake   = wNow;
            tn.vBrake   = 0.5f * (tn.wl.filt + tn.wr.filt) * 100.0f / TICKS_PER_CM;
            handOver(tn.vBrake, tn.dir, &tn.wl, &tn.wr, tn.lastPL, tn.lastPR);
            tn.active   = 0;
            break;
        }
#endif
        if ((!tn.flyOut && remaining <= stopDeg) || now > tn.deadline) {
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
        float slowTo = sqrtf(2.0f * TURN_DECEL *
                       fmaxf(remCm - TURN_APPROACH_CM, 0.0f)) + TURN_V_MIN;
        if (tn.flyOut) slowTo = sqrtf(2.0f * TURN_DECEL * fmaxf(remCm, 0.0f)) + tn.vExit;
        float upTo   = fminf(tn.v + TURN_ACCEL * 0.01f, TURN_V_MAX);
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
        tn.lastPL = pL;
        tn.lastPR = pR;
        writeServo(tn.lockUs);
        break;
    }

    case TN_BRAKE:
    default:
        if (el < MOVE_BRAKE_MS) {                      // active brake
            setMotorPwm(PWM_BRAKE, PWM_BRAKE, PWM_BRAKE, PWM_BRAKE);
            tn.stillLoops = 0;
            break;
        }
        setMotorPwm(0, 0, 0, 0);
        tn.stillLoops = (cntL == 0 && cntR == 0) ? (uint8_t)(tn.stillLoops + 1) : 0;
        if (tn.stillLoops >= MOVE_STILL_LOOPS || el > 700) {
            exitV = 0.0f;                              // stopped: nothing to hand over
            tn.resAngle = turned;
            tn.resErr   = headingDeg - tn.targetAbs;
            if (tn.endSteerUs && !tn.aborted) {
                preSteer(tn.endSteerUs);               // step 9: the next turn uses this lock too
            } else {
                // Start bringing the wheels back towards straight (from below, like
                // a straight's settle does) so the next straight can start sooner.
                writeServo(SERVO_TRUE_CENTRE + servoTrim - SERVO_APPROACH_US);
                preSteerUs = 0;
            }
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
    uint16_t endSteer = takeEndSteer();                // step 9 (dropped if this move doesn't run)
    uint8_t  fly      = takeFly();                     // Banzai (dropped likewise)
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
    int swing    = abs(cur - (int)tn.lockUs);           // servo travel needed (us)
#if TURN_SETTLE_PROP
    // TURN_SETTLE_MS covers a full lock-to-lock swing; a shorter swing needs less
    tn.settleMs  = (swing > 20) ? (uint16_t)(60 + (TURN_SETTLE_MS - 60) * swing / (SERVO_RIGHT_LOCK - SERVO_LEFT_LOCK)) : 60;
    if (tn.settleMs > TURN_SETTLE_MS) tn.settleMs = TURN_SETTLE_MS;
#else
    tn.settleMs  = (swing > 20) ? TURN_SETTLE_MS : 60;
#endif
    if (preSteerUs == tn.lockUs && swing <= 20) {      // step 9: the last move already swung the servo here
        uint32_t need = swingSettleMs(preSteerSwing);  // ... so only wait for what is left of that swing
        uint32_t el   = HAL_GetTick() - preSteerTick;
        tn.settleMs   = (el + 60u >= need) ? 60u : (uint16_t)(need - el);
    }
    preSteerUs    = 0;
    tn.endSteerUs = endSteer;
    tn.flyIn  = fly & 1;
    tn.flyOut = (fly >> 1) & 1;
#ifdef T2B_FLY_V
    tn.vExit  = T2B_FLY_V;
#endif
    if (tn.flyIn && exitV > 0.0f && exitDir == ((dir > 0) ? 1 : -1)) {
        tn.settleMs = 0;                               // Banzai: already rolling; the servo swings on the move
    } else {
        tn.flyIn = 0;
    }
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
    setLastEnd(tn.aborted, tn.timedOut);
    lastRes.dist = 0.0f;
    lastRes.u = -1.0f;
    lastRes.stopBy = 'T';
    lastRes.faceSeen = 0;
    lastRes.target = 0.0f;
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

    poseStep(cntL, cntR);                            // tracked x, y, every 10 ms, moving or not

    if (tn.active) {
        turnStep(cntL, cntR);
        return;
    }
    if (mv.active) {
        straightStep(cntL, cntR);
        return;
    }

    // Idle: motors off. A '!' while idle also straightens the wheels.
    // Banzai: right after a flying hand-over, keep driving for a moment so the
    // robot doesn't coast while the command task sets up the next move.
    if (exitV > 0.0f) {
        if ((int32_t)(flyHoldUntil - HAL_GetTick()) > 0 && !emergency_stop_requested) {
            driveWheel(1, exitDir, holdPL, 0);
            driveWheel(0, exitDir, holdPR, 0);
            return;
        }
        exitV = 0.0f;                                  // no move followed (abort / stop): brake, don't coast
        flyBrakeUntil = HAL_GetTick() + 300;
    }
    if ((int32_t)(flyBrakeUntil - HAL_GetTick()) > 0) {
        setMotorPwm(PWM_BRAKE, PWM_BRAKE, PWM_BRAKE, PWM_BRAKE);
    } else {
        setMotorPwm(0, 0, 0, 0);
    }
    if (emergency_stop_requested) {
        writeServo(SERVO_TRUE_CENTRE);
        emergency_stop_requested = 0;
    }
}

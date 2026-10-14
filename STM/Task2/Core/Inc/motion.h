/*
 * motion.h
 *
 *  Created on: Sep 27, 2026
 *      Author: Ryan Tan
 *
 * Straights, turns and heading for the robot.
 *
 * How it works: the command task (or the field tools) calls Motion_Straight()
 * or Motion_Turn(). Those set the move up and then wait. The actual driving
 * happens 10 ms at a time inside Motion_Step(), which the motor task calls,
 * so encoder data never has to cross between tasks.
 */
#ifndef MOTION_H
#define MOTION_H

#include <stdint.h>

/* ---- Called by the motor task every 10 ms -------------------------------- */
void Motion_Step(int16_t cntL, int16_t cntR);   // encoder ticks since the last call

/* ---- Moves: block until the robot has stopped ----------------------------- */
void Motion_Straight(float distance_cm);         // negative = backwards
void Motion_Turn(int left, int dir, float angleDeg);   // left: 1/0, dir: +1 fwd / -1 reverse
void Motion_ZeroHeading(void);                    // "ZH": the current direction becomes north, pose back to the start
void Motion_GetPose(float *x, float *y, float *h); // tracked rear-axle pose (cm, cm, deg); any task, never waits
uint8_t Motion_IsBusy(void);
void Motion_EndSteer(int lock);                   // step 9: the NEXT move ends with the servo at this lock
                                                  // (1 left, 0 right, -1 none), ready for the turn after it
void Motion_Fly(int flyIn, int flyOut);           // Banzai: the NEXT move starts at the speed the last one handed
                                                  // over (no stop, no settle) and/or ends at speed (no brake)

/* ---- Calibration tests (floor) --------------------------------------------- */
void Motion_VmaxTest(float maxCm, char *reply, unsigned len);   // VMAX?: ramp until a wheel saturates or it yaws
void Motion_BrakeTest(float v);                   // VB<v>: 100 cm at v, hard brake at 100 (reply: Motion_Reply)

/* ---- Sensor-ended straights (forward). The sensors decide WHERE to stop;
 *      the encoders still do the stopping. All distances are cm of travel. --- */
typedef struct {
    float  maxCm;       // Stop here at the latest (the cap while no IR face has been seen)
    float  minCm;       // Never stop before this, unless an echo is under ULTRA_HARD_STOP_CM
    float  vCap;        // Cruise speed cap (cm/s)
    float  usStop;      // Stop when the ultrasonic reads this (cm); 0 = off
    float  ignBelow;    // Ignore echoes shorter than this ...
    float  ignUntil;    // ... until this much travel
    int8_t edgeSide;    // IR_LEFT / IR_RIGHT: stop edgeExtra after this IR loses a face; -1 = off
    float  edgeExtra;
    float  edgeMaxCm;   // Cap once that IR has seen a face (replaces maxCm)
    float  edgeFromCm;  // Ignore "ends" seen before this much travel (the image card at the face's centre); 0 = off
    int8_t guardSide;   // IR_LEFT / IR_RIGHT: stop at once if it reads under guardCm; -1 = off
    float  guardCm;
    float  guardFromCm; // ... checked only after this much travel
    float  usEmergCm;   // Stop at once if two echoes in a row are under this; 0 = off
    float  slowZoneCm;  // Drive the last slowZoneCm at no more than vSlow; 0 = off
    float  vSlow;
    int8_t reverse;     // 1 = drive backwards (the ultrasonic stop is then ignored: it looks forward)
    float  brakeAtCm;   // Banzai: no slow-down; hard brake when this close to the stop point (cm); 0 = normal stop
    int8_t watchSide;   // Banzai: IR_LEFT / IR_RIGHT: report the longest steady stretch of this IR (MoveResult_t); -1 = off
    float  watchTol;    // ... readings within this of the stretch's mean so far (cm; a few odd ones are skipped)
} Until_t;

void Motion_UntilInit(Until_t *o);                 // everything off, vCap = STRAIGHT_V_MAX
void Motion_StraightOpts(const Until_t *o);        // blocks until stopped; reply "d= e= u= by="

/* SU / SW: forward until the ultrasonic reads usStopCm, never before minCm or
 * after maxCm, cruise capped at vCap. Echoes shorter than ignBelowCm are
 * ignored for the first ignUntilCm. */
void Motion_StraightUntil(float usStopCm, float minCm, float maxCm, float vCap,
                          float ignBelowCm, float ignUntilCm);

/* What the last move did, for the Task 2 sequencer */
#define MOVE_OK       0
#define MOVE_ABORT    1           // '!' or the button
#define MOVE_TIMEOUT  2
#define MOVE_BUSY     3
typedef struct {
    uint8_t end;                  // MOVE_OK / MOVE_ABORT / MOVE_TIMEOUT / MOVE_BUSY
    float   dist;                 // straights: cm travelled
    float   u;                    // sensor straights: ultrasonic at the stop, -1 if none
    char    stopBy;               // sensor straights: U ultrasonic, E IR edge, C cap, F floor, G guard
    uint8_t faceSeen;             // sensor straights: the edge IR saw a face
    float   target;               // sensor straights: where the sensors said to stop (cm of travel)
    float   watchSpan;            // watchSide: longest stretch of travel with a steady reading (cm), 0 = none
    float   watchMean;            // ... its average reading (cm)
} MoveResult_t;
void Motion_LastResult(MoveResult_t *r);

/* ---- Results of the last move --------------------------------------------- */
const char *Motion_Reply(void);   // for the RPi (short unless DEBUG_REPLIES)
const char *Motion_LogText(void); // full diagnostics, for the TD log

/* ---- Shared with the gyro task and OLED ----------------------------------- */
extern volatile float   headingDeg;   // written by the gyro task (float: one 32-bit write)
extern float            headingZero;  // gyro heading at the last ZH
extern volatile uint8_t robotStill;   // 1 once the wheels haven't turned for 0.5 s

/* ---- Sensors, owned by main.c (sensor task + TIM5 echo interrupt) -------- */
#define IR_LEFT   0                       // IR1
#define IR_RIGHT  1                       // IR2
extern volatile float    irCm[2];         // cm from the lens, IR_CLEAR if > 60 cm (median of 3 updates)
extern volatile float    irVolts[2];      // raw averaged volts
extern volatile uint32_t irSeq;           // +1 per IR update (every IR_PERIOD_MS)
uint32_t Ultra_Get(float *cm);            // latest echo: returns its sequence number; *cm < 0 if stale / no echo
float    Ultra_Query(int pings, int *nGood);       // TASKS ONLY (waits): median of fresh pings, -1 if none
void     IR_Query(int updates, float out[2]);      // TASKS ONLY (waits): median of fresh IR updates

#endif /* MOTION_H */

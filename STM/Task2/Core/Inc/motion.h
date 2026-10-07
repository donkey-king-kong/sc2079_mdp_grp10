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
void Motion_ZeroHeading(void);                    // "ZH": the current direction becomes north
uint8_t Motion_IsBusy(void);

/* ---- Results of the last move --------------------------------------------- */
const char *Motion_Reply(void);   // for the RPi (short unless DEBUG_REPLIES)
const char *Motion_LogText(void); // full diagnostics, for the TD log

/* ---- Shared with the gyro task and OLED ----------------------------------- */
extern volatile float   headingDeg;   // written by the gyro task (float: one 32-bit write)
extern float            headingZero;  // gyro heading at the last ZH
extern volatile uint8_t robotStill;   // 1 once the wheels haven't turned for 0.5 s

#endif /* MOTION_H */

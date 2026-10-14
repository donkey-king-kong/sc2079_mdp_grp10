/*
 * task2.h
 *
 * Task 2 sequencer (TASK2_DESIGN.md v2). The RPi drives it with three
 * commands; everything between them runs here, on the tracked pose.
 *
 *   ZH   (main.c)  zero heading and pose; sequencer back to "waiting for GO"
 *   GO             drive to scan 1                     -> "A S1 u=<cm> by=<c>"
 *   AL / AR        arrow 1: dodge, drive to scan 2, turn to face the card  -> "A S2 u=<cm> w=<cm> by=<c> a=<deg>"
 *   AL / AR        arrow 2: round obstacle 2, park     -> "A FIN x=<cm> y=<cm> h=<deg> by=<c> end=<cm> re=<n>"
 *   GL / GR        (Banzai 2) GO with arrow 1 known: no stop at scan 1 -> the "A S2 ..." reply (+ u1=)
 *   '!'            stops the current move; the reply ends in "ABORT at <move>"
 *
 * Bench helpers: SEL / SER = one "along the face" leg (next turn left / right).
 * All functions block (they run moves): call them from the command task only.
 */
#ifndef TASK2_H
#define TASK2_H

#include <stddef.h>

void Task2_Reset(void);                                  // called by ZH
void Task2_Go(char *reply, size_t len);                  // GO
void Task2_Arrow(int left, char *reply, size_t len);     // AL (left = 1) / AR (left = 0)
void Task2_GoArrow(int left, char *reply, size_t len);   // GL / GR: GO + arrow 1 in one go (Banzai 2)
void Task2_AlongFace(int turnLeft);                      // SEL / SER bench test (reply: Motion_Reply)

#endif /* TASK2_H */

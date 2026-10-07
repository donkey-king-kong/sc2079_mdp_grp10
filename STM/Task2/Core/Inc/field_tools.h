/*
 * field_tools.h
 *
 *  Created on: Sep 27, 2026
 *      Author: Ryan Tan
 *
 * Knob-and-button test modes for calibration, used without a cable:
 *   SD  straight drive: the knob picks -100..+100 cm, a tap drives it
 *   TN  turn:           the knob picks a turn (LF090, RB045, ...), a tap runs it
 *   Hold the button ~1 s to switch between SD and TN. Tap during a move to stop it.
 *
 * Serial commands (work whether or not the robot is being driven by hand):
 *   TD       print every result since power-up (last TEST_LOG_SIZE kept)
 *   TV<us>   set the knob value by hand (fallback if the knob fails)
 *
 * Everything here is compiled only when FIELD_TOOLS = 1 in robot_config.h,
 * except Board_ReadAdc(), which main.c provides for the battery reading too.
 */
#ifndef FIELD_TOOLS_H
#define FIELD_TOOLS_H

#include <stdint.h>
#include <stddef.h>

/* Provided by main.c: reads one ADC1 channel, 0xFFFFFFFF on failure. */
uint32_t Board_ReadAdc(uint32_t channel);

void FieldTools_Init(void);          // before the scheduler starts: PB1 knob as analog input
void FieldTools_UpdateKnob(void);    // from the OLED task, ~10 Hz
void FieldTools_Run(void);           // body of the default task; never returns

/* Handles the serial "T" commands (TD, TV). Writes the reply text (without the
 * leading "A ") into reply. Returns 1 if the command was recognised. */
int  FieldTools_Command(char sub, int value, char *reply, size_t replyLen);

#endif /* FIELD_TOOLS_H */

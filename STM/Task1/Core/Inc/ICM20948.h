/*
 * ICM20948.h
 *
 * Register map and functions for the ICM-20948 IMU on I2C2 (gyro and
 * accelerometer only). See ICM20948.c.
 */

#ifndef INC_ICM20948_H_
#define INC_ICM20948_H_

// Includes
#include "main.h"
#include <string.h>
#include <stdio.h>      // sprintf
#include <stdbool.h>    // bool

#define ICM20948_I2C_ADDR           (0x68 << 1)     // 0xD0: 7-bit address 0x68, shifted for the HAL

#define REG_BANK_SEL                0x7F            // Select USER_BANK[1:0], bits [5:4]

// Bank 0
#define WHO_AM_I                    0x00            // WHO_AM_I register
#define ICM20948_WHO_AM_I_VAL       0xEA            // Expected WHO_AM_I value
//#define USER_CTRL                 0x03
#define PWR_MGMT_1                  0x06            // Reset, sleep and clock source
#define PWR_MGMT_2                  0x07            // Enable/disable accel and gyro axes
#define INT_PIN_CFG                 0x0F            // Interrupt pin config
#define INT_ENABLE_1                0x11            // Raw data ready interrupt enable

// Bank 2
#define GYRO_SMPLRT_DIV             0x00            // Gyro sample rate divider
#define GYRO_CONFIG_1               0x01            // Gyro range and low-pass filter
#define ODR_ALIGN_EN                0x09            // Start time alignment
#define ACCEL_SMPLRT_DIV_1          0x10            // Accel sample rate divider (MSB)
#define ACCEL_SMPLRT_DIV_2          0x11            // Accel sample rate divider (LSB)
#define ACCEL_CONFIG                0x14            // Accel range and low-pass filter

typedef enum
{
    _gyro_250dps,
    _gyro_500dps,
    _gyro_1000dps,
    _gyro_2000dps
} gyro_range;

typedef enum
{
    _accel_2g,
    _accel_4g,
    _accel_8g,
    _accel_16g
} accel_range;

typedef struct {
    float x_acc;    // g
    float y_acc;
    float z_acc;
    float x_gyro;   // deg/s
    float y_gyro;
    float z_gyro;
} ICM20948_Data;

HAL_StatusTypeDef ICM20948_Init(void);      // HAL_ERROR if WHO_AM_I does not match
void ICM_ReadData(ICM20948_Data* data);     // reads all six axes

#endif /* INC_ICM20948_H_ */

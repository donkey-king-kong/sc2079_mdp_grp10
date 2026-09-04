/*
 * ICM20948.h
 */


#ifndef INC_ICM20948_H_
#define INC_ICM20948_H_

// Includes
#include "main.h"
#include <string.h>   // for sprintf
#include <stdio.h>
#include <stdbool.h>   // to support boolean data type

#define ICM20948_I2C_ADDR			(0x68 << 1) // 0xD0

#define REG_BANK_SEL             	0x7F 			// Select USER_BANK[1:0], bits[5:4]

// Bank 0
#define WHO_AM_I					0x00			// WHO AM I reg
#define ICM20948_WHO_AM_I_VAL 		0xEA			// WHO AM I value
//#define USER_CTRL					0x03
#define PWR_MGMT_1					0x06			// Device mode
#define PWR_MGMT_2					0x07			// Enable/Disable Acc/Gyro
#define INT_PIN_CFG					0x0F			// Interrupt pin config
#define INT_ENABLE_1				0x11			// Interrupt pin enable

// Bank 2
#define GYRO_SMPLRT_DIV				0x00			// Gyro sample rate divider
#define GYRO_CONFIG_1				0x01			// Gyro LPF
#define ODR_ALIGN_EN				0x09			// Start time alignment
#define ACCEL_SMPLRT_DIV_1			0x10			// Acc sample rate divider (MSB)
#define ACCEL_SMPLRT_DIV_2			0x11			// Acc sample rate divider (LSB)
#define ACCEL_CONFIG				0x14			// Acc LPF

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
	float x_acc;
	float y_acc;
	float z_acc;
	float x_gyro;
	float y_gyro;
	float z_gyro;
} ICM20948_Data;

HAL_StatusTypeDef ICM20948_Init(void);
void ICM_ReadData(ICM20948_Data* data);

#endif /* INC_ICM20948_H_ */

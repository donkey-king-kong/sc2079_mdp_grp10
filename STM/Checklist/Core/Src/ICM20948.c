#include "ICM20948.h"

/*
HAL_StatusTypeDef HAL_I2C_Mem_Write(
	I2C_HandleTypeDef *hi2c,
    uint16_t DevAddress,				// Target device address
    uint16_t MemAddress,				// Internal memory address
    uint16_t MemAddSize,				// Internal memory size
    uint8_t *pData,						// Pointer to data buffer
    uint16_t Size,						// Buffer size
    uint32_t Timeout					// Timeout
);

										//Returns HAL_Status

*/

extern I2C_HandleTypeDef hi2c2; // from main.c

HAL_StatusTypeDef ret; // to store return status
uint8_t dat;

gyro_range GYRO_RANGE_VALUE = _gyro_500dps;		// Gyro range
accel_range ACCEL_RANGE_VALUE = _accel_4g;		// Acc range

float GYRO_SCALE = 65.5;
float ACCEL_SCALE = 8192.0;

void ICM_SelBank(uint8_t bank);

static HAL_StatusTypeDef ICM_WriteReg(uint8_t reg, uint8_t dat) {
	return HAL_I2C_Mem_Write(&hi2c2, ICM20948_I2C_ADDR, reg, I2C_MEMADD_SIZE_8BIT, &dat, 1, 10);
}

static uint8_t ICM_ReadReg(uint8_t reg) {
	uint8_t dat = 0;
	HAL_I2C_Mem_Read(&hi2c2, ICM20948_I2C_ADDR, reg, I2C_MEMADD_SIZE_8BIT, &dat, 1, 10);
	return dat;
}

// TODO: ICM_ReadData
void ICM_ReadData(ICM20948_Data* data)
{
	uint8_t buf[12];
	ICM_SelBank(0);

	HAL_I2C_Mem_Read(&hi2c2, ICM20948_I2C_ADDR, 0x2D, I2C_MEMADD_SIZE_8BIT, buf, 12, 10);
	//check for status?

	int16_t raw_x_acc = (int16_t)(buf[0] << 8) | buf[1];
	int16_t raw_y_acc = (int16_t)(buf[2] << 8) | buf[3];
	int16_t raw_z_acc = (int16_t)(buf[4] << 8) | buf[5];

	int16_t raw_x_gyro = (int16_t)(buf[6] << 8) | buf[7];
	int16_t raw_y_gyro = (int16_t)(buf[8] << 8) | buf[9];
	int16_t raw_z_gyro = (int16_t)(buf[10] << 8) | buf[11];

	// convert LSB to dps (degree per sec)
	data -> x_acc = raw_x_acc / ACCEL_SCALE;
	data -> y_acc = raw_y_acc / ACCEL_SCALE;
	data -> z_acc = raw_z_acc / ACCEL_SCALE;

	// convert LSB to g (1g = 9.81m/s^2)
	data -> x_gyro = raw_x_gyro / GYRO_SCALE;
	data -> y_gyro = raw_y_gyro / GYRO_SCALE;
	data -> z_gyro = raw_z_gyro / GYRO_SCALE;



}

void ICM_SelBank(uint8_t bank) {
	uint8_t bank_val = (bank << 4) & 0x30;		// Bit extraction
	ICM_WriteReg(REG_BANK_SEL, bank_val);
}

HAL_StatusTypeDef ICM20948_Init(void)
{
	ICM_SelBank(0);												// Select Bank 0

	if (ICM_ReadReg(WHO_AM_I) != ICM20948_WHO_AM_I_VAL)							// Verify WHO_AM_I ID
		return HAL_ERROR;

	ICM_WriteReg(PWR_MGMT_1, 0xc1);								// IMU reset
	HAL_Delay(100);
	ICM_WriteReg(PWR_MGMT_1, 0x01);								// Exit power mode

	ICM_SelBank(2);												// Select Bank 2
	ICM_WriteReg(ODR_ALIGN_EN, 0x01);							// Sync sensors

	ICM_WriteReg(GYRO_SMPLRT_DIV, 0x00);						// Gyro sample rate divider = 0
	ICM_WriteReg(GYRO_CONFIG_1, ((GYRO_RANGE_VALUE << 1)|0x01));// Gyro config, set range, enable LPF

	ICM_WriteReg(ACCEL_SMPLRT_DIV_1, 0x00);						// Acc sample rate divider = 0
	ICM_WriteReg(ACCEL_SMPLRT_DIV_2, 0x00);						// ^^
	ICM_WriteReg(ACCEL_CONFIG, ((ACCEL_RANGE_VALUE << 1)|0x01));// Acc config, set range, enable LPF

	ICM_SelBank(0);												// Select Bank 0
	ICM_WriteReg(INT_PIN_CFG, 0x00);							// Interrupt pin: Active High, Push-pull, 50us pulse
	ICM_WriteReg(INT_ENABLE_1, 0x01);							// Enable interrupt

	return HAL_OK;
}

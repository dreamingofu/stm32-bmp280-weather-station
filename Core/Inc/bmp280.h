#ifndef BMP280_H
#define BMP280_H

#include "stm32f4xx_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Load factory calibration and enable normal mode with 1x oversampling.
 * This driver supports one BMP280 at address 0x76. Call before reading data.
 */
void BMP280_Init(I2C_HandleTypeDef *i2c);

/**
 * Read compensated temperature in degrees Celsius and pressure in hPa.
 * Callers must serialize access to the shared I2C bus. Output pointers must
 * be valid. Transfers use HAL_MAX_DELAY; HAL errors are not reported.
 */
void BMP280_ReadData(float *temperature_c, float *pressure_hpa);

#ifdef __cplusplus
}
#endif

#endif /* BMP280_H */

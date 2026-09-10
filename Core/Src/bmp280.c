#include "bmp280.h"

/* STM32 HAL expects the 7-bit device address shifted left by one. */
#define BMP280_I2C_ADDRESS (0x76U << 1)
#define BMP280_REG_CALIBRATION 0x88U
#define BMP280_REG_CONTROL 0xF4U
#define BMP280_REG_PRESSURE_DATA 0xF7U
#define BMP280_NORMAL_MODE_1X 0x27U
#define BMP280_CALIBRATION_BYTES 26U
#define BMP280_SAMPLE_BYTES 6U

/* Names match the factory coefficients in the BMP280 compensation formula. */
typedef struct {
    uint16_t dig_T1;
    int16_t dig_T2;
    int16_t dig_T3;
    uint16_t dig_P1;
    int16_t dig_P2;
    int16_t dig_P3;
    int16_t dig_P4;
    int16_t dig_P5;
    int16_t dig_P6;
    int16_t dig_P7;
    int16_t dig_P8;
    int16_t dig_P9;
} BMP280_Calibration;

static I2C_HandleTypeDef *sensor_i2c;
static BMP280_Calibration calibration;

static uint16_t ReadLittleEndian16(const uint8_t *bytes)
{
    return (uint16_t)((bytes[1] << 8) | bytes[0]);
}

static void ReadCalibration(void)
{
    uint8_t bytes[BMP280_CALIBRATION_BYTES];
    uint8_t reg = BMP280_REG_CALIBRATION;

    HAL_I2C_Master_Transmit(sensor_i2c, BMP280_I2C_ADDRESS, &reg, 1,
                            HAL_MAX_DELAY);
    HAL_I2C_Master_Receive(sensor_i2c, BMP280_I2C_ADDRESS, bytes,
                           sizeof(bytes), HAL_MAX_DELAY);

    /* Only the first 24 bytes hold the temperature/pressure coefficients. */
    calibration.dig_T1 = ReadLittleEndian16(&bytes[0]);
    calibration.dig_T2 = (int16_t)ReadLittleEndian16(&bytes[2]);
    calibration.dig_T3 = (int16_t)ReadLittleEndian16(&bytes[4]);
    calibration.dig_P1 = ReadLittleEndian16(&bytes[6]);
    calibration.dig_P2 = (int16_t)ReadLittleEndian16(&bytes[8]);
    calibration.dig_P3 = (int16_t)ReadLittleEndian16(&bytes[10]);
    calibration.dig_P4 = (int16_t)ReadLittleEndian16(&bytes[12]);
    calibration.dig_P5 = (int16_t)ReadLittleEndian16(&bytes[14]);
    calibration.dig_P6 = (int16_t)ReadLittleEndian16(&bytes[16]);
    calibration.dig_P7 = (int16_t)ReadLittleEndian16(&bytes[18]);
    calibration.dig_P8 = (int16_t)ReadLittleEndian16(&bytes[20]);
    calibration.dig_P9 = (int16_t)ReadLittleEndian16(&bytes[22]);
}

void BMP280_Init(I2C_HandleTypeDef *i2c)
{
    sensor_i2c = i2c;
    ReadCalibration();

    /* Temperature 1x, pressure 1x, continuous normal mode. */
    uint8_t config[] = {BMP280_REG_CONTROL, BMP280_NORMAL_MODE_1X};
    HAL_I2C_Master_Transmit(sensor_i2c, BMP280_I2C_ADDRESS, config,
                            sizeof(config), HAL_MAX_DELAY);
}

void BMP280_ReadData(float *temperature_c, float *pressure_hpa)
{
    uint8_t data[BMP280_SAMPLE_BYTES];
    uint8_t reg = BMP280_REG_PRESSURE_DATA;

    HAL_I2C_Master_Transmit(sensor_i2c, BMP280_I2C_ADDRESS, &reg, 1,
                            HAL_MAX_DELAY);
    HAL_I2C_Master_Receive(sensor_i2c, BMP280_I2C_ADDRESS, data,
                           sizeof(data), HAL_MAX_DELAY);

    /* Both ADC readings are 20-bit values; pressure appears first. */
    int32_t adc_P = (int32_t)((data[0] << 12) | (data[1] << 4) | (data[2] >> 4));
    int32_t adc_T = (int32_t)((data[3] << 12) | (data[4] << 4) | (data[5] >> 4));

    int32_t var1, var2;
    var1 = ((((adc_T >> 3) - ((int32_t)calibration.dig_T1 << 1))) *
            ((int32_t)calibration.dig_T2)) >> 11;
    var2 = (((((adc_T >> 4) - ((int32_t)calibration.dig_T1)) *
              ((adc_T >> 4) - ((int32_t)calibration.dig_T1))) >> 12) *
            ((int32_t)calibration.dig_T3)) >> 14;

    /* t_fine carries temperature compensation into the pressure formula. */
    int32_t t_fine = var1 + var2;
    *temperature_c = (t_fine * 5 + 128) >> 8;
    *temperature_c /= 100.0f; /* Integer result is in hundredths of a degree. */

    /* Keep the original 64-bit fixed-point operation order and rounding. */
    int64_t var1_p, var2_p, p;
    var1_p = ((int64_t)t_fine) - 128000;
    var2_p = var1_p * var1_p * (int64_t)calibration.dig_P6;
    var2_p = var2_p + ((var1_p * (int64_t)calibration.dig_P5) << 17);
    var2_p = var2_p + (((int64_t)calibration.dig_P4) << 35);
    var1_p = ((var1_p * var1_p * (int64_t)calibration.dig_P3) >> 8) +
             ((var1_p * (int64_t)calibration.dig_P2) << 12);
    var1_p = (((((int64_t)1) << 47) + var1_p)) *
             ((int64_t)calibration.dig_P1) >> 33;
    if (var1_p == 0) {
        *pressure_hpa = 0;
        return;
    }
    p = 1048576 - adc_P;
    p = (((p << 31) - var2_p) * 3125) / var1_p;
    var1_p = (((int64_t)calibration.dig_P9) * (p >> 13) * (p >> 13)) >> 25;
    var2_p = (((int64_t)calibration.dig_P8) * p) >> 19;
    p = ((p + var1_p + var2_p) >> 8) + (((int64_t)calibration.dig_P7) << 4);
    *pressure_hpa = (float)p / 25600.0f; /* Q24.8 pascals to hPa. */
}

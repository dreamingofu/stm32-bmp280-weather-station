#include "bmp280.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Keep checks active in Release builds, where assert() would be disabled. */
#define CHECK(condition)                                                       \
    do {                                                                       \
        if (!(condition)) {                                                    \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition);      \
            exit(EXIT_FAILURE);                                                \
        }                                                                      \
    } while (0)

static I2C_HandleTypeDef test_i2c;
static uint8_t calibration_bytes[26];
static uint8_t sample_bytes[6];
static unsigned transaction;

/* The first set is Bosch's BMP280 compensation example (section 3.12):
 * https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bmp280-ds001.pdf
 * The second exercises another calibration, including signed coefficients. */
static const int32_t calibration_sets[][12] = {
    {27504, 26435, -1000, 36477, -10685, 3024, 2855, 140, -7, 15500, -14600, 6000},
    {30000, 25000, -1200, 35000, -10000, 2900, 2700, 150, -8, 15000, -14000, 5500}
};

typedef struct {
    unsigned calibration_index;
    uint32_t raw_temperature;
    uint32_t raw_pressure;
    float temperature_c;
    float pressure_hpa;
} ReadingFixture;

/* Golden outputs captured by compiling the original main.c compensation
 * functions before extraction. Hex literals retain the exact float results;
 * no second copy of the compensation algorithm is needed in this test. */
static const ReadingFixture fixtures[] = {
    /* Bosch example: 25.08 C and approximately 1006.5327 hPa. */
    {0, 519888, 415148, 0x1.9147aep+4f, 0x1.f74428p+9f},
    {0, 480000, 300000, 0x1.923d7p+3f, 0x1.27ab22p+10f},
    {0, 550000, 500000, 0x1.14147ap+5f, 0x1.b45aecp+9f},
    {0, 400000, 450000, -0x1.947ae2p+3f, 0x1.be5f4cp+9f},
    {0, 600000, 350000, 0x1.90e148p+5f, 0x1.22a7fap+10f},
    {0, 519888, 415149, 0x1.9147aep+4f, 0x1.f743fp+9f},
    {1, 519888, 415148, 0x1.7bd70ap+3f, 0x1.02577p+10f},
    {1, 480000, 300000, 0x0p+0f, 0x1.2fdffap+10f},
    {1, 550000, 500000, 0x1.4ca3d8p+4f, 0x1.bfbabep+9f},
    {1, 400000, 450000, -0x1.7ee148p+4f, 0x1.cd4934p+9f},
    {1, 600000, 350000, 0x1.1c8f5cp+5f, 0x1.290752p+10f},
    {1, 519888, 415149, 0x1.7bd70ap+3f, 0x1.025754p+10f}
};

HAL_StatusTypeDef HAL_I2C_Master_Transmit(I2C_HandleTypeDef *i2c,
                                       uint16_t address,
                                       uint8_t *data,
                                       uint16_t size,
                                       uint32_t timeout)
{
    CHECK(i2c == &test_i2c);
    CHECK(address == (0x76U << 1));
    CHECK(timeout == HAL_MAX_DELAY);

    switch (transaction) {
    case 0:
        CHECK(size == 1);
        CHECK(data[0] == 0x88);
        break;
    case 2:
        CHECK(size == 2);
        CHECK(data[0] == 0xf4);
        CHECK(data[1] == 0x27);
        break;
    case 3:
        CHECK(size == 1);
        CHECK(data[0] == 0xf7);
        break;
    default:
        CHECK(0 && "Unexpected I2C transmit");
    }
    ++transaction;
    return HAL_OK;
}

HAL_StatusTypeDef HAL_I2C_Master_Receive(I2C_HandleTypeDef *i2c,
                                      uint16_t address,
                                      uint8_t *data,
                                      uint16_t size,
                                      uint32_t timeout)
{
    CHECK(i2c == &test_i2c);
    CHECK(address == (0x76U << 1));
    CHECK(timeout == HAL_MAX_DELAY);

    if (transaction == 1) {
        CHECK(size == sizeof(calibration_bytes));
        memcpy(data, calibration_bytes, size);
    } else {
        CHECK(transaction == 4);
        CHECK(size == sizeof(sample_bytes));
        memcpy(data, sample_bytes, size);
    }
    ++transaction;
    return HAL_OK;
}

static void LoadCalibration(unsigned index)
{
    for (unsigned i = 0; i < 12; ++i) {
        uint16_t coefficient = (uint16_t)calibration_sets[index][i];
        calibration_bytes[i * 2] = (uint8_t)coefficient;
        calibration_bytes[i * 2 + 1] = (uint8_t)(coefficient >> 8);
    }
    /* These bytes are read but do not belong to the T/P coefficients. */
    calibration_bytes[24] = 0xaa;
    calibration_bytes[25] = 0x55;
}

static void LoadRawSample(uint32_t temperature, uint32_t pressure)
{
    sample_bytes[0] = (uint8_t)(pressure >> 12);
    sample_bytes[1] = (uint8_t)(pressure >> 4);
    sample_bytes[2] = (uint8_t)((pressure & 0x0f) << 4) | 0x0f;
    sample_bytes[3] = (uint8_t)(temperature >> 12);
    sample_bytes[4] = (uint8_t)(temperature >> 4);
    sample_bytes[5] = (uint8_t)((temperature & 0x0f) << 4) | 0x0f;
    /* Set unused low nibbles to verify they are excluded from the ADC value. */
}

static void TestRegressionReadings(void)
{
    for (unsigned i = 0; i < sizeof(fixtures) / sizeof(fixtures[0]); ++i) {
        const ReadingFixture *fixture = &fixtures[i];
        LoadCalibration(fixture->calibration_index);
        LoadRawSample(fixture->raw_temperature, fixture->raw_pressure);

        transaction = 0;
        BMP280_Init(&test_i2c);
        CHECK(transaction == 3);

        float temperature_c = 0;
        float pressure_hpa = 0;
        BMP280_ReadData(&temperature_c, &pressure_hpa);
        CHECK(transaction == 5);
        CHECK(temperature_c == fixture->temperature_c);
        CHECK(pressure_hpa == fixture->pressure_hpa);
    }
}

static void TestZeroPressureDivisor(void)
{
    LoadCalibration(0);
    calibration_bytes[6] = 0; /* dig_P1 = 0 makes the pressure divisor zero. */
    calibration_bytes[7] = 0;
    LoadRawSample(519888, 415148);

    transaction = 0;
    BMP280_Init(&test_i2c);

    float temperature_c = 0;
    float pressure_hpa = -1;
    BMP280_ReadData(&temperature_c, &pressure_hpa);
    CHECK(transaction == 5);
    CHECK(temperature_c == 25.08f);
    CHECK(pressure_hpa == 0.0f);
}

int main(void)
{
    TestRegressionReadings();
    TestZeroPressureDivisor();
    puts("BMP280: 12 regression readings, I2C sequence, and zero divisor passed.");
    return EXIT_SUCCESS;
}

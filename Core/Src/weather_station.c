/**
 * @file weather_station.c
 * @brief FreeRTOS tasks and shared resources for the weather station.
 *
 * The sensor task feeds a FIFO consumed by the display task. The UART task
 * takes its own readings. One mutex serializes all runtime I2C transactions.
 */
#include "weather_station.h"

#include <stdio.h>
#include <string.h>

#include "bmp280.h"
#include "cmsis_os.h"
#include "main.h"
#include "ssd1306.h"

/* osDelay() takes ticks; FreeRTOSConfig.h configures a 1 kHz tick. */
#define SENSOR_DELAY_TICKS 500U
#define UART_DELAY_TICKS   1000U
#define SENSOR_QUEUE_DEPTH 5U

typedef struct {
    float temperature_c;
    float pressure_hpa;
} SensorReading;

static I2C_HandleTypeDef *i2c_bus;
static UART_HandleTypeDef *serial_port;
static osMessageQueueId_t sensor_queue;
static osMutexId_t i2c_mutex;

/* Only the ISR writes this byte. Volatile exposes changes to the tasks;
 * it is not a general synchronization mechanism for shared data. */
static volatile uint8_t use_fahrenheit = 0U;

/* CMSIS-RTOS2 stack sizes are bytes. Keep the original task allocations. */
static const osThreadAttr_t sensor_task_attributes = {
    .name = "sensorTask",
    .stack_size = 1024U,
    .priority = osPriorityAboveNormal,
};

static const osThreadAttr_t display_task_attributes = {
    .name = "displayTask",
    .stack_size = 2048U,
    .priority = osPriorityNormal,
};

static const osThreadAttr_t uart_task_attributes = {
    .name = "uartTask",
    .stack_size = 2048U,
    .priority = osPriorityNormal,
};

static void SensorTask(void *argument);
static void DisplayTask(void *argument);
static void UartTask(void *argument);

void WeatherStation_CreateResources(I2C_HandleTypeDef *i2c, UART_HandleTypeDef *uart)
{
    const osMutexAttr_t mutex_attributes = {.name = "i2cMutex"};

    i2c_bus = i2c;
    serial_port = uart;
    i2c_mutex = osMutexNew(&mutex_attributes);
    sensor_queue = osMessageQueueNew(SENSOR_QUEUE_DEPTH, sizeof(SensorReading), NULL);
}

void WeatherStation_StartTasks(void)
{
    osThreadNew(SensorTask, NULL, &sensor_task_attributes);
    osThreadNew(DisplayTask, NULL, &display_task_attributes);
    osThreadNew(UartTask, NULL, &uart_task_attributes);
}

void WeatherStation_ToggleTemperatureUnit(void)
{
    use_fahrenheit = !use_fahrenheit;
}

static float CelsiusToFahrenheit(float temperature_c)
{
    return temperature_c * 9.0f / 5.0f + 32.0f;
}

/* Capture a reading, then release the bus before publishing it to the display. */
static void SensorTask(void *argument)
{
    SensorReading reading;

    (void)argument;

    for (;;) {
        if (osMutexAcquire(i2c_mutex, osWaitForever) == osOK) {
            BMP280_ReadData(&reading.temperature_c, &reading.pressure_hpa);
            osMutexRelease(i2c_mutex);
        }

        /* Nonblocking FIFO write: a full queue drops this new sample. */
        osMessageQueuePut(sensor_queue, &reading, 0, 0);

        osDelay(SENSOR_DELAY_TICKS);
    }
}

/* Only this task owns the OLED framebuffer once the scheduler has started. */
static void DisplayTask(void *argument)
{
    SensorReading reading;
    char line[22];

    (void)argument;

    for (;;) {
        if (osMessageQueueGet(sensor_queue, &reading, NULL, osWaitForever) == osOK) {
            float display_temperature = reading.temperature_c;
            const char *unit = "C";

            if (use_fahrenheit) {
                display_temperature = CelsiusToFahrenheit(reading.temperature_c);
                unit = "F";
            }

            if (osMutexAcquire(i2c_mutex, osWaitForever) == osOK) {
                SSD1306_Clear();

                SSD1306_SetCursor(0, 0);
                SSD1306_WriteString("BMP280 Weather");

                SSD1306_SetCursor(0, 2);
                sprintf(line, "Temp: %.1f %s", display_temperature, unit);
                SSD1306_WriteString(line);

                SSD1306_SetCursor(0, 4);
                sprintf(line, "Pres: %.1f hPa", reading.pressure_hpa);
                SSD1306_WriteString(line);

                SSD1306_SetCursor(0, 6);
                SSD1306_WriteString(use_fahrenheit ? "[F mode]" : "[C mode]");

                SSD1306_UpdateScreen(i2c_bus);

                osMutexRelease(i2c_mutex);
            }

            HAL_GPIO_TogglePin(LD2_GPIO_Port, LD2_Pin);
        }
    }
}

/* Read independently so UART logging never consumes a display queue message. */
static void UartTask(void *argument)
{
    char uart_buffer[100];
    SensorReading last_reading = {0};

    (void)argument;

    for (;;) {
        if (osMutexAcquire(i2c_mutex, osWaitForever) == osOK) {
            BMP280_ReadData(&last_reading.temperature_c, &last_reading.pressure_hpa);
            osMutexRelease(i2c_mutex);
        }

        float display_temperature = last_reading.temperature_c;
        const char *unit = "C";
        if (use_fahrenheit) {
            display_temperature = CelsiusToFahrenheit(last_reading.temperature_c);
            unit = "F";
        }

        sprintf(uart_buffer, "Temp: %.2f %s  Pressure: %.2f hPa\r\n",
                display_temperature, unit, last_reading.pressure_hpa);
        HAL_UART_Transmit(serial_port, (uint8_t *)uart_buffer,
                          strlen(uart_buffer), HAL_MAX_DELAY);

        osDelay(UART_DELAY_TICKS);
    }
}

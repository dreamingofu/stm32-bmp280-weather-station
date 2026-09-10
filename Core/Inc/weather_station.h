#ifndef WEATHER_STATION_H
#define WEATHER_STATION_H

#include "stm32f4xx_hal.h"

/* Call once after device initialization and osKernelInitialize(). */
void WeatherStation_CreateResources(I2C_HandleTypeDef *i2c, UART_HandleTypeDef *uart);

/* Create the sensor, display, and UART threads before osKernelStart(). */
void WeatherStation_StartTasks(void);

/* Called by the button ISR; output tasks observe changes on their next cycle. */
void WeatherStation_ToggleTemperatureUnit(void);

#endif /* WEATHER_STATION_H */

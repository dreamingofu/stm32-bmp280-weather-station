#ifndef TEST_STM32F4XX_HAL_H
#define TEST_STM32F4XX_HAL_H

#include <stdint.h>

/* Only the HAL surface used by the drivers and application tasks is mocked. */
typedef struct {
    uint32_t instance;
} I2C_HandleTypeDef;

typedef struct {
    uint32_t instance;
} UART_HandleTypeDef;

typedef struct {
    uint32_t instance;
} GPIO_TypeDef;

extern GPIO_TypeDef test_gpioa;
#define GPIOA (&test_gpioa)
#define GPIO_PIN_5 ((uint16_t)0x0020)

typedef enum {
    HAL_OK = 0,
    HAL_ERROR = 1,
    HAL_BUSY = 2,
    HAL_TIMEOUT = 3
} HAL_StatusTypeDef;

#define HAL_MAX_DELAY UINT32_MAX

HAL_StatusTypeDef HAL_I2C_Master_Transmit(I2C_HandleTypeDef *i2c,
                                       uint16_t address,
                                       uint8_t *data,
                                       uint16_t size,
                                       uint32_t timeout);
HAL_StatusTypeDef HAL_I2C_Master_Receive(I2C_HandleTypeDef *i2c,
                                      uint16_t address,
                                      uint8_t *data,
                                      uint16_t size,
                                      uint32_t timeout);
void HAL_Delay(uint32_t delay);
HAL_StatusTypeDef HAL_UART_Transmit(UART_HandleTypeDef *uart,
                                  uint8_t *data,
                                  uint16_t size,
                                  uint32_t timeout);
void HAL_GPIO_TogglePin(GPIO_TypeDef *port, uint16_t pin);

#endif

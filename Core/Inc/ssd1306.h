#ifndef SSD1306_H
#define SSD1306_H

#include "stm32f4xx_hal.h"

/* STM32 HAL expects the 7-bit I2C address shifted left by one bit. */
#define SSD1306_ADDR        (0x3C << 1)
#define SSD1306_WIDTH       128
#define SSD1306_HEIGHT      64

/*
 * Single-display driver with a shared 1024-byte framebuffer.
 * Initialize the HAL I2C peripheral before calling SSD1306_Init.
 * Transfers block with HAL_MAX_DELAY; this API does not report HAL errors.
 */

/* Configure the display, reset the cursor, and transmit a blank framebuffer. */
void SSD1306_Init(I2C_HandleTypeDef *hi2c);

/* Clear the local framebuffer and reset the cursor; no I2C transfer occurs. */
void SSD1306_Clear(void);

/*
 * Send all eight pages to the display. Pass the same bus used for Init:
 * commands use the saved bus, while pixel data uses this argument.
 */
void SSD1306_UpdateScreen(I2C_HandleTypeDef *hi2c);

/* Set column x (0-127) and eight-pixel page y (0-7), without range validation. */
void SSD1306_SetCursor(uint8_t x, uint8_t y);

/*
 * Draw printable ASCII (32-126) in a six-column cell and advance the cursor.
 * Wrap at the right edge; skip drawing if the resulting page is past the bottom.
 * Unsupported bytes, including newlines, do not move the cursor.
 */
void SSD1306_WriteChar(char ch);

/* Draw a non-NULL, NUL-terminated string locally; call UpdateScreen to show it. */
void SSD1306_WriteString(const char *str);

#endif

#include "ssd1306.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition);   \
            exit(EXIT_FAILURE);                                              \
        }                                                                    \
    } while (0)

enum {
    PAGE_COUNT = 8,
    FRAME_SIZE = SSD1306_WIDTH * PAGE_COUNT,
    MAX_TRANSFERS = 64,
    MAX_PACKET_SIZE = SSD1306_WIDTH + 1
};

typedef struct {
    I2C_HandleTypeDef *bus;
    uint16_t address;
    uint16_t size;
    uint32_t timeout;
    uint8_t bytes[MAX_PACKET_SIZE];
} Transfer;

static I2C_HandleTypeDef display_bus;
static I2C_HandleTypeDef alternate_bus;
static Transfer transfers[MAX_TRANSFERS];
static size_t transfer_count;
static size_t delay_count;

HAL_StatusTypeDef HAL_I2C_Master_Transmit(I2C_HandleTypeDef *bus,
                                        uint16_t address,
                                        uint8_t *data,
                                        uint16_t size,
                                        uint32_t timeout)
{
    CHECK(transfer_count < MAX_TRANSFERS);
    CHECK(size <= MAX_PACKET_SIZE);
    Transfer *transfer = &transfers[transfer_count++];
    transfer->bus = bus;
    transfer->address = address;
    transfer->size = size;
    transfer->timeout = timeout;
    memcpy(transfer->bytes, data, size);
    return HAL_OK;
}

void HAL_Delay(uint32_t milliseconds)
{
    CHECK(milliseconds == 100);
    CHECK(transfer_count == 0);
    delay_count++;
}

static void check_transfer(size_t index, I2C_HandleTypeDef *bus, uint16_t size)
{
    CHECK(index < transfer_count);
    CHECK(transfers[index].bus == bus);
    CHECK(transfers[index].address == (0x3C << 1));
    CHECK(transfers[index].size == size);
    CHECK(transfers[index].timeout == HAL_MAX_DELAY);
}

static void check_command(size_t index, uint8_t command)
{
    check_transfer(index, &display_bus, 2);
    CHECK(transfers[index].bytes[0] == 0x00);
    CHECK(transfers[index].bytes[1] == command);
}

/* Each page uses three address commands followed by one 128-byte payload. */
static void check_frame(size_t first_transfer,
                        I2C_HandleTypeDef *data_bus,
                        const uint8_t expected[FRAME_SIZE])
{
    CHECK(transfer_count == first_transfer + PAGE_COUNT * 4);
    for (size_t page = 0; page < PAGE_COUNT; page++) {
        size_t index = first_transfer + page * 4;
        check_command(index, (uint8_t)(0xB0 + page));
        check_command(index + 1, 0x00);
        check_command(index + 2, 0x10);
        check_transfer(index + 3, data_bus, SSD1306_WIDTH + 1);
        CHECK(transfers[index + 3].bytes[0] == 0x40);
        CHECK(memcmp(&transfers[index + 3].bytes[1],
                     &expected[page * SSD1306_WIDTH], SSD1306_WIDTH) == 0);
    }
}

static void test_initialization(void)
{
    static const uint8_t expected_commands[] = {
        0xAE, 0x20, 0x00, 0xB0, 0xC8, 0x00, 0x10, 0x40,
        0x81, 0xFF, 0xA1, 0xA6, 0xA8, 0x3F, 0xA4, 0xD3,
        0x00, 0xD5, 0xF0, 0xD9, 0x22, 0xDA, 0x12, 0xDB,
        0x20, 0x8D, 0x14, 0xAF
    };
    const uint8_t blank_frame[FRAME_SIZE] = {0};

    SSD1306_Init(&display_bus);

    CHECK(delay_count == 1);
    for (size_t index = 0; index < sizeof(expected_commands); index++) {
        check_command(index, expected_commands[index]);
    }
    check_frame(sizeof(expected_commands), &display_bus, blank_frame);
}

static void test_clear_resets_pixels_and_cursor(void)
{
    const uint8_t glyph_a[] = {0x7E, 0x11, 0x11, 0x11, 0x7E};
    uint8_t expected[FRAME_SIZE] = {0};
    transfer_count = 0;

    SSD1306_WriteString("AB");
    SSD1306_SetCursor(20, 4);
    SSD1306_WriteChar('Z');
    SSD1306_Clear();
    SSD1306_WriteChar('A');
    CHECK(transfer_count == 0);

    memcpy(expected, glyph_a, sizeof(glyph_a));
    SSD1306_UpdateScreen(&display_bus);
    check_frame(0, &display_bus, expected);
}

static void test_text_wrap_and_unsupported_characters(void)
{
    const uint8_t glyph_a[] = {0x7E, 0x11, 0x11, 0x11, 0x7E};
    const uint8_t glyph_b[] = {0x7F, 0x49, 0x49, 0x49, 0x36};
    const uint8_t glyph_tilde[] = {0x08, 0x08, 0x2A, 0x1C, 0x08};
    uint8_t expected[FRAME_SIZE] = {0};
    transfer_count = 0;

    SSD1306_Clear();
    SSD1306_SetCursor(122, 2);
    SSD1306_WriteString("A\n\x7f" "B");
    SSD1306_WriteChar((char)0xFF);
    SSD1306_WriteString(" ~");
    CHECK(transfer_count == 0);

    memcpy(&expected[2 * SSD1306_WIDTH + 122], glyph_a, sizeof(glyph_a));
    memcpy(&expected[3 * SSD1306_WIDTH], glyph_b, sizeof(glyph_b));
    memcpy(&expected[3 * SSD1306_WIDTH + 12], glyph_tilde, sizeof(glyph_tilde));
    /* Existing API sends commands on the initialized bus and data on this bus. */
    SSD1306_UpdateScreen(&alternate_bus);
    check_frame(0, &alternate_bus, expected);
}

static void test_bottom_edge_and_spacing(void)
{
    const uint8_t glyph_b[] = {0x7F, 0x49, 0x49, 0x49, 0x36};
    uint8_t expected[FRAME_SIZE] = {0};
    transfer_count = 0;

    SSD1306_Clear();
    SSD1306_SetCursor(5, 7);
    SSD1306_WriteChar('A');
    SSD1306_SetCursor(0, 7);
    SSD1306_WriteChar('B');
    SSD1306_SetCursor(123, 7);
    SSD1306_WriteString("AB");
    SSD1306_SetCursor(0, 8);
    SSD1306_WriteChar('A');

    memcpy(&expected[7 * SSD1306_WIDTH], glyph_b, sizeof(glyph_b));
    /* B's blank sixth column overwrites A's first column at x=5. */
    expected[7 * SSD1306_WIDTH + 6] = 0x11;
    expected[7 * SSD1306_WIDTH + 7] = 0x11;
    expected[7 * SSD1306_WIDTH + 8] = 0x11;
    expected[7 * SSD1306_WIDTH + 9] = 0x7E;
    SSD1306_UpdateScreen(&display_bus);
    check_frame(0, &display_bus, expected);
}

int main(void)
{
    test_initialization();
    test_clear_resets_pixels_and_cursor();
    test_text_wrap_and_unsupported_characters();
    test_bottom_edge_and_spacing();
    puts("SSD1306 tests passed");
    return EXIT_SUCCESS;
}

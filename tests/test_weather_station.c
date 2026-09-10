#include "weather_station.h"

#include <setjmp.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bmp280.h"
#include "cmsis_os.h"
#include "main.h"
#include "ssd1306.h"

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition);   \
            exit(EXIT_FAILURE);                                              \
        }                                                                    \
    } while (0)

enum { SENSOR_TASK, DISPLAY_TASK, UART_TASK, TASK_COUNT };

typedef struct {
    osThreadFunc_t callback;
    osThreadAttr_t attributes;
} CapturedTask;

typedef struct {
    unsigned reads, puts, gets, acquires, releases;
    unsigned clears, cursors, lines, updates, leds, uart_writes;
    uint32_t delay_ticks;
    float published[2];
    char display_lines[4][22];
    char uart_text[100];
} Observations;

GPIO_TypeDef test_gpioa;
static I2C_HandleTypeDef test_i2c;
static UART_HandleTypeDef test_uart;
static uint8_t mutex_token, queue_token;
static bool mutex_held;
static unsigned mutex_creations, queue_creations, task_count;
static CapturedTask tasks[TASK_COUNT];
static Observations observed;
static jmp_buf iteration_finished;
static osStatus_t put_status = osOK;

/* Distinct inputs expose any accidental use of the display queue for UART. */
static const float sensor_sample[2] = {21.25f, 1001.5f};
static const float display_sample[2] = {25.0f, 1013.5f};

osMutexId_t osMutexNew(const osMutexAttr_t *attributes)
{
    CHECK(strcmp(attributes->name, "i2cMutex") == 0);
    CHECK(attributes->attr_bits == 0);
    CHECK(attributes->cb_mem == NULL && attributes->cb_size == 0);
    mutex_creations++;
    return &mutex_token;
}

osMessageQueueId_t osMessageQueueNew(uint32_t count, uint32_t size,
                                   const osMessageQueueAttr_t *attributes)
{
    CHECK(count == 5);
    CHECK(size == sizeof(sensor_sample));
    CHECK(attributes == NULL);
    queue_creations++;
    return &queue_token;
}

osThreadId_t osThreadNew(osThreadFunc_t callback, void *argument,
                        const osThreadAttr_t *attributes)
{
    CHECK(task_count < TASK_COUNT);
    CHECK(callback != NULL && argument == NULL);
    tasks[task_count].callback = callback;
    tasks[task_count].attributes = *attributes;
    return &tasks[task_count++];
}

osStatus_t osMutexAcquire(osMutexId_t mutex, uint32_t timeout)
{
    CHECK(mutex == &mutex_token && timeout == osWaitForever);
    CHECK(!mutex_held);
    mutex_held = true;
    observed.acquires++;
    return osOK;
}

osStatus_t osMutexRelease(osMutexId_t mutex)
{
    CHECK(mutex == &mutex_token && mutex_held);
    mutex_held = false;
    observed.releases++;
    return osOK;
}

osStatus_t osMessageQueuePut(osMessageQueueId_t queue, const void *message,
                            uint8_t priority, uint32_t timeout)
{
    CHECK(queue == &queue_token && priority == 0 && timeout == 0);
    CHECK(!mutex_held);
    memcpy(observed.published, message, sizeof(observed.published));
    observed.puts++;
    return put_status;
}

osStatus_t osMessageQueueGet(osMessageQueueId_t queue, void *message,
                            uint8_t *priority, uint32_t timeout)
{
    CHECK(queue == &queue_token && priority == NULL && timeout == osWaitForever);
    CHECK(!mutex_held);
    if (++observed.gets == 2) {
        longjmp(iteration_finished, 1);
    }
    memcpy(message, display_sample, sizeof(display_sample));
    return osOK;
}

osStatus_t osDelay(uint32_t ticks)
{
    CHECK(!mutex_held);
    observed.delay_ticks = ticks;
    longjmp(iteration_finished, 1);
}

void BMP280_ReadData(float *temperature, float *pressure)
{
    CHECK(mutex_held);
    *temperature = sensor_sample[0];
    *pressure = sensor_sample[1];
    observed.reads++;
}

void SSD1306_Clear(void)
{
    CHECK(mutex_held);
    observed.clears++;
}

void SSD1306_SetCursor(uint8_t x, uint8_t y)
{
    CHECK(mutex_held && observed.clears == 1);
    CHECK(x == 0 && y == observed.cursors * 2);
    observed.cursors++;
}

void SSD1306_WriteString(const char *text)
{
    CHECK(mutex_held && observed.lines < 4);
    CHECK(observed.cursors == observed.lines + 1);
    CHECK(strlen(text) < sizeof(observed.display_lines[0]));
    strcpy(observed.display_lines[observed.lines++], text);
}

void SSD1306_UpdateScreen(I2C_HandleTypeDef *i2c)
{
    CHECK(mutex_held && i2c == &test_i2c);
    CHECK(observed.clears == 1 && observed.lines == 4);
    observed.updates++;
}

void HAL_GPIO_TogglePin(GPIO_TypeDef *port, uint16_t pin)
{
    CHECK(!mutex_held && observed.updates == 1);
    CHECK(port == GPIOA && pin == GPIO_PIN_5);
    observed.leds++;
}

HAL_StatusTypeDef HAL_UART_Transmit(UART_HandleTypeDef *uart, uint8_t *data,
                                  uint16_t size, uint32_t timeout)
{
    CHECK(!mutex_held && uart == &test_uart && timeout == HAL_MAX_DELAY);
    CHECK(size < sizeof(observed.uart_text));
    memcpy(observed.uart_text, data, size);
    observed.uart_text[size] = '\0';
    observed.uart_writes++;
    return HAL_OK;
}

/* Infinite callbacks stop at their delay or next blocking queue read. */
static void run_one_iteration(unsigned task)
{
    memset(&observed, 0, sizeof(observed));
    CHECK(!mutex_held);
    if (setjmp(iteration_finished) == 0) {
        tasks[task].callback(NULL);
        CHECK(false);
    }
    CHECK(!mutex_held);
    CHECK(observed.acquires == 1 && observed.releases == 1);
}

static void test_resource_and_task_configuration(void)
{
    const char *names[] = {"sensorTask", "displayTask", "uartTask"};
    const uint32_t stack_sizes[] = {1024, 2048, 2048};
    const osPriority_t priorities[] = {
        osPriorityAboveNormal, osPriorityNormal, osPriorityNormal
    };

    WeatherStation_CreateResources(&test_i2c, &test_uart);
    WeatherStation_StartTasks();
    CHECK(mutex_creations == 1 && queue_creations == 1);
    CHECK(task_count == TASK_COUNT);
    for (unsigned task = 0; task < TASK_COUNT; task++) {
        const osThreadAttr_t *attributes = &tasks[task].attributes;
        CHECK(strcmp(attributes->name, names[task]) == 0);
        CHECK(attributes->stack_size == stack_sizes[task]);
        CHECK(attributes->priority == priorities[task]);
        CHECK(attributes->attr_bits == 0 && attributes->cb_mem == NULL);
        CHECK(attributes->cb_size == 0 && attributes->stack_mem == NULL);
        CHECK(attributes->tz_module == 0 && attributes->reserved == 0);
    }
}

static void test_sensor_publishes_once_even_when_queue_is_full(void)
{
    const osStatus_t outcomes[] = {osOK, osErrorResource};
    for (unsigned outcome = 0; outcome < 2; outcome++) {
        put_status = outcomes[outcome];
        run_one_iteration(SENSOR_TASK);
        CHECK(observed.reads == 1 && observed.puts == 1 && observed.gets == 0);
        CHECK(observed.published[0] == sensor_sample[0]);
        CHECK(observed.published[1] == sensor_sample[1]);
        CHECK(observed.delay_ticks == 500);
        CHECK(observed.updates == 0 && observed.uart_writes == 0);
    }
    put_status = osOK;
}

static void test_display_celsius_and_fahrenheit(void)
{
    const char *temperatures[] = {"Temp: 25.0 C", "Temp: 77.0 F"};
    const char *modes[] = {"[C mode]", "[F mode]"};
    for (unsigned unit = 0; unit < 2; unit++) {
        run_one_iteration(DISPLAY_TASK);
        CHECK(observed.reads == 0 && observed.puts == 0 && observed.gets == 2);
        CHECK(observed.updates == 1 && observed.leds == 1);
        CHECK(observed.delay_ticks == 0 && observed.uart_writes == 0);
        CHECK(strcmp(observed.display_lines[0], "BMP280 Weather") == 0);
        CHECK(strcmp(observed.display_lines[1], temperatures[unit]) == 0);
        CHECK(strcmp(observed.display_lines[2], "Pres: 1013.5 hPa") == 0);
        CHECK(strcmp(observed.display_lines[3], modes[unit]) == 0);
        WeatherStation_ToggleTemperatureUnit();
    }
}

static void test_uart_reads_independently_in_both_units(void)
{
    const char *outputs[] = {
        "Temp: 21.25 C  Pressure: 1001.50 hPa\r\n",
        "Temp: 70.25 F  Pressure: 1001.50 hPa\r\n"
    };
    for (unsigned unit = 0; unit < 2; unit++) {
        run_one_iteration(UART_TASK);
        CHECK(observed.reads == 1 && observed.puts == 0 && observed.gets == 0);
        CHECK(observed.uart_writes == 1 && observed.updates == 0);
        CHECK(observed.delay_ticks == 1000);
        CHECK(strcmp(observed.uart_text, outputs[unit]) == 0);
        WeatherStation_ToggleTemperatureUnit();
    }
}

int main(void)
{
    test_resource_and_task_configuration();
    test_sensor_publishes_once_even_when_queue_is_full();
    test_display_celsius_and_fahrenheit();
    test_uart_reads_independently_in_both_units();
    puts("Weather station task tests passed");
    return EXIT_SUCCESS;
}

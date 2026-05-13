/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "cmsis_os.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <string.h>
#include <stdio.h>
#include "ssd1306.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
typedef struct {
    float temperature;
    float pressure;
} SensorData_t;
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define BMP280_ADDR       (0x76 << 1)
#define BMP280_REG_ID     0xD0
#define BMP280_REG_CTRL   0xF4
#define BMP280_REG_DATA   0xF7
#define BMP280_REG_CALIB  0x88
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
I2C_HandleTypeDef hi2c1;

UART_HandleTypeDef huart2;

/* Definitions for defaultTask */
osThreadId_t defaultTaskHandle;
const osThreadAttr_t defaultTask_attributes = {
  .name = "defaultTask",
  .stack_size = 128 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};
/* USER CODE BEGIN PV */

// BMP280 calibration data
uint16_t dig_T1;
int16_t  dig_T2, dig_T3;
uint16_t dig_P1;
int16_t  dig_P2, dig_P3, dig_P4, dig_P5, dig_P6, dig_P7, dig_P8, dig_P9;
int32_t  t_fine;

// RTOS objects
osMessageQueueId_t sensorQueueHandle;
osMutexId_t i2cMutexHandle;

// Task handles
osThreadId_t sensorTaskHandle;
osThreadId_t displayTaskHandle;
osThreadId_t uartTaskHandle;

// Task attributes
const osThreadAttr_t sensorTask_attributes = {
    .name = "sensorTask",
    .stack_size = 256 * 4,
    .priority = (osPriority_t) osPriorityAboveNormal,
};

const osThreadAttr_t displayTask_attributes = {
    .name = "displayTask",
    .stack_size = 512 * 4,
    .priority = (osPriority_t) osPriorityNormal,
};

const osThreadAttr_t uartTask_attributes = {
    .name = "uartTask",
    .stack_size = 512 * 4,
    .priority = (osPriority_t) osPriorityNormal,
};

// Shared flag for temperature unit toggle (set by button interrupt)
volatile uint8_t use_fahrenheit = 0;

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_USART2_UART_Init(void);
static void MX_I2C1_Init(void);
void StartDefaultTask(void *argument);

/* USER CODE BEGIN PFP */
void SensorTask(void *argument);
void DisplayTask(void *argument);
void UartTask(void *argument);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
void BMP280_ReadCalibration(void)
{
    uint8_t calib[26];
    uint8_t reg = BMP280_REG_CALIB;

    HAL_I2C_Master_Transmit(&hi2c1, BMP280_ADDR, &reg, 1, HAL_MAX_DELAY);
    HAL_I2C_Master_Receive(&hi2c1, BMP280_ADDR, calib, 26, HAL_MAX_DELAY);

    dig_T1 = (uint16_t)(calib[1] << 8 | calib[0]);
    dig_T2 = (int16_t)(calib[3] << 8 | calib[2]);
    dig_T3 = (int16_t)(calib[5] << 8 | calib[4]);
    dig_P1 = (uint16_t)(calib[7] << 8 | calib[6]);
    dig_P2 = (int16_t)(calib[9] << 8 | calib[8]);
    dig_P3 = (int16_t)(calib[11] << 8 | calib[10]);
    dig_P4 = (int16_t)(calib[13] << 8 | calib[12]);
    dig_P5 = (int16_t)(calib[15] << 8 | calib[14]);
    dig_P6 = (int16_t)(calib[17] << 8 | calib[16]);
    dig_P7 = (int16_t)(calib[19] << 8 | calib[18]);
    dig_P8 = (int16_t)(calib[21] << 8 | calib[20]);
    dig_P9 = (int16_t)(calib[23] << 8 | calib[22]);
}

void BMP280_ReadData(float *temperature, float *pressure)
{
    uint8_t data[6];
    uint8_t reg = BMP280_REG_DATA;

    HAL_I2C_Master_Transmit(&hi2c1, BMP280_ADDR, &reg, 1, HAL_MAX_DELAY);
    HAL_I2C_Master_Receive(&hi2c1, BMP280_ADDR, data, 6, HAL_MAX_DELAY);

    int32_t adc_P = (int32_t)((data[0] << 12) | (data[1] << 4) | (data[2] >> 4));
    int32_t adc_T = (int32_t)((data[3] << 12) | (data[4] << 4) | (data[5] >> 4));

    int32_t var1, var2;
    var1 = ((((adc_T >> 3) - ((int32_t)dig_T1 << 1))) * ((int32_t)dig_T2)) >> 11;
    var2 = (((((adc_T >> 4) - ((int32_t)dig_T1)) * ((adc_T >> 4) - ((int32_t)dig_T1))) >> 12) * ((int32_t)dig_T3)) >> 14;
    t_fine = var1 + var2;
    *temperature = (t_fine * 5 + 128) >> 8;
    *temperature /= 100.0f;

    int64_t var1_p, var2_p, p;
    var1_p = ((int64_t)t_fine) - 128000;
    var2_p = var1_p * var1_p * (int64_t)dig_P6;
    var2_p = var2_p + ((var1_p * (int64_t)dig_P5) << 17);
    var2_p = var2_p + (((int64_t)dig_P4) << 35);
    var1_p = ((var1_p * var1_p * (int64_t)dig_P3) >> 8) + ((var1_p * (int64_t)dig_P2) << 12);
    var1_p = (((((int64_t)1) << 47) + var1_p)) * ((int64_t)dig_P1) >> 33;
    if (var1_p == 0) {
        *pressure = 0;
        return;
    }
    p = 1048576 - adc_P;
    p = (((p << 31) - var2_p) * 3125) / var1_p;
    var1_p = (((int64_t)dig_P9) * (p >> 13) * (p >> 13)) >> 25;
    var2_p = (((int64_t)dig_P8) * p) >> 19;
    p = ((p + var1_p + var2_p) >> 8) + (((int64_t)dig_P7) << 4);
    *pressure = (float)p / 25600.0f;
}

// Button interrupt callback
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    if (GPIO_Pin == B1_Pin) {
        use_fahrenheit = !use_fahrenheit;
    }
}

// ============================================================
// SENSOR TASK: Reads BMP280 every 500ms, sends data to queue
// ============================================================
void SensorTask(void *argument)
{
    SensorData_t data;

    for (;;) {
        // Take the I2C mutex before accessing the bus
        if (osMutexAcquire(i2cMutexHandle, osWaitForever) == osOK) {
            BMP280_ReadData(&data.temperature, &data.pressure);
            osMutexRelease(i2cMutexHandle);
        }

        // Send data to queue (overwrite if full so display always gets latest)
        osMessageQueuePut(sensorQueueHandle, &data, 0, 0);

        osDelay(500); // Read sensor every 500ms
    }
}

// ============================================================
// DISPLAY TASK: Updates OLED with latest sensor data
// ============================================================
void DisplayTask(void *argument)
{
    SensorData_t data;
    char line[22];

    for (;;) {
        // Wait for new data from the queue
        if (osMessageQueueGet(sensorQueueHandle, &data, NULL, osWaitForever) == osOK) {

            float display_temp = data.temperature;
            const char *unit = "C";

            if (use_fahrenheit) {
                display_temp = data.temperature * 9.0f / 5.0f + 32.0f;
                unit = "F";
            }

            // Take the I2C mutex before updating the display
            if (osMutexAcquire(i2cMutexHandle, osWaitForever) == osOK) {
                SSD1306_Clear();

                SSD1306_SetCursor(0, 0);
                SSD1306_WriteString("BMP280 Weather");

                SSD1306_SetCursor(0, 2);
                sprintf(line, "Temp: %.1f %s", display_temp, unit);
                SSD1306_WriteString(line);

                SSD1306_SetCursor(0, 4);
                sprintf(line, "Pres: %.1f hPa", data.pressure);
                SSD1306_WriteString(line);

                SSD1306_SetCursor(0, 6);
                SSD1306_WriteString(use_fahrenheit ? "[F mode]" : "[C mode]");

                SSD1306_UpdateScreen(&hi2c1);

                osMutexRelease(i2cMutexHandle);
            }

            // Toggle LED to show the system is alive
            HAL_GPIO_TogglePin(GPIOA, GPIO_PIN_5);
        }
    }
}

// ============================================================
// UART TASK: Sends sensor data over serial for PC logging
// ============================================================
void UartTask(void *argument)
{
    SensorData_t data;
    char uart_buf[100];
    SensorData_t last_data = {0};

    for (;;) {
        // Peek the queue - don't consume the message (display task needs it)
        // Instead, we use a separate copy from shared memory
        // Simple approach: just read the latest data periodically

        // Take mutex to read sensor
        if (osMutexAcquire(i2cMutexHandle, osWaitForever) == osOK) {
            BMP280_ReadData(&last_data.temperature, &last_data.pressure);
            osMutexRelease(i2cMutexHandle);
        }

        float display_temp = last_data.temperature;
        const char *unit = "C";
        if (use_fahrenheit) {
            display_temp = last_data.temperature * 9.0f / 5.0f + 32.0f;
            unit = "F";
        }

        sprintf(uart_buf, "Temp: %.2f %s  Pressure: %.2f hPa\r\n", display_temp, unit, last_data.pressure);
        HAL_UART_Transmit(&huart2, (uint8_t *)uart_buf, strlen(uart_buf), HAL_MAX_DELAY);

        osDelay(1000); // Send UART data every 1 second
    }
}

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_USART2_UART_Init();
  MX_I2C1_Init();
  /* USER CODE BEGIN 2 */

  // Initialize OLED
  SSD1306_Init(&hi2c1);

  // Read BMP280 calibration data and configure sensor
  BMP280_ReadCalibration();
  uint8_t config[2] = {BMP280_REG_CTRL, 0x27};
  HAL_I2C_Master_Transmit(&hi2c1, BMP280_ADDR, config, 2, HAL_MAX_DELAY);

  // Enable button interrupt
  HAL_NVIC_SetPriority(EXTI15_10_IRQn, 5, 0);
  HAL_NVIC_EnableIRQ(EXTI15_10_IRQn);

  HAL_UART_Transmit(&huart2, (uint8_t *)"FreeRTOS Weather Station starting...\r\n", 38, HAL_MAX_DELAY);

  /* USER CODE END 2 */

  /* Init scheduler */
  osKernelInitialize();

  /* USER CODE BEGIN RTOS_MUTEX */
  // Create I2C mutex
  const osMutexAttr_t i2cMutex_attributes = {
      .name = "i2cMutex"
  };
  i2cMutexHandle = osMutexNew(&i2cMutex_attributes);
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* USER CODE END RTOS_TIMERS */

  /* USER CODE BEGIN RTOS_QUEUES */
  // Create sensor data queue (holds up to 5 readings)
  sensorQueueHandle = osMessageQueueNew(5, sizeof(SensorData_t), NULL);
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* creation of defaultTask */
  defaultTaskHandle = osThreadNew(StartDefaultTask, NULL, &defaultTask_attributes);

  /* USER CODE BEGIN RTOS_THREADS */
  sensorTaskHandle = osThreadNew(SensorTask, NULL, &sensorTask_attributes);
  displayTaskHandle = osThreadNew(DisplayTask, NULL, &displayTask_attributes);
  uartTaskHandle = osThreadNew(UartTask, NULL, &uartTask_attributes);
  /* USER CODE END RTOS_THREADS */

  /* USER CODE BEGIN RTOS_EVENTS */
  /* USER CODE END RTOS_EVENTS */

  /* Start scheduler */
  osKernelStart();

  /* We should never get here as control is now taken by the scheduler */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE3);

  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLM = 16;
  RCC_OscInitStruct.PLL.PLLN = 336;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV4;
  RCC_OscInitStruct.PLL.PLLQ = 2;
  RCC_OscInitStruct.PLL.PLLR = 2;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
}

static void MX_I2C1_Init(void)
{
  hi2c1.Instance = I2C1;
  hi2c1.Init.ClockSpeed = 100000;
  hi2c1.Init.DutyCycle = I2C_DUTYCYCLE_2;
  hi2c1.Init.OwnAddress1 = 0;
  hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c1.Init.OwnAddress2 = 0;
  hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c1) != HAL_OK)
  {
    Error_Handler();
  }
}

static void MX_USART2_UART_Init(void)
{
  huart2.Instance = USART2;
  huart2.Init.BaudRate = 115200;
  huart2.Init.WordLength = UART_WORDLENGTH_8B;
  huart2.Init.StopBits = UART_STOPBITS_1;
  huart2.Init.Parity = UART_PARITY_NONE;
  huart2.Init.Mode = UART_MODE_TX_RX;
  huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart2.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart2) != HAL_OK)
  {
    Error_Handler();
  }
}

static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin, GPIO_PIN_RESET);

  GPIO_InitStruct.Pin = B1_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(B1_GPIO_Port, &GPIO_InitStruct);

  GPIO_InitStruct.Pin = LD2_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(LD2_GPIO_Port, &GPIO_InitStruct);
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/* USER CODE BEGIN Header_StartDefaultTask */
/**
  * @brief  Function implementing the defaultTask thread.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_StartDefaultTask */
void StartDefaultTask(void *argument)
{
  /* USER CODE BEGIN 5 */
  // Default task does nothing - our work is in the other tasks
  for(;;)
  {
    osDelay(1000);
  }
  /* USER CODE END 5 */
}

void Error_Handler(void)
{
  __disable_irq();
  while (1)
  {
  }
}

#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line)
{
}
#endif /* USE_FULL_ASSERT */

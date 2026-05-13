# STM32 BMP280 Weather Station (FreeRTOS)

Real-time temperature and pressure monitoring using an STM32F446RE microcontroller, BMP280 sensor, and SSD1306 OLED display — running on FreeRTOS with concurrent tasks, shared resource protection, and interrupt-driven input.

![Status](https://img.shields.io/badge/status-working-brightgreen)
![RTOS](https://img.shields.io/badge/RTOS-FreeRTOS-blue)

## What It Does

Reads temperature and barometric pressure from a BMP280 sensor, displays live data on a 0.96" OLED screen, and streams readings over UART — all running as independent FreeRTOS tasks. Press the blue user button to toggle between Celsius and Fahrenheit in real time.

## Architecture

```
[Button ISR] --toggles unit flag--> [Display Task]
                                         |
[Sensor Task] --queue--> [Display Task] --I2C (mutex)--> OLED
                                   
[UART Task] --I2C (mutex)--> BMP280 --UART--> PC

All I2C bus access protected by a shared mutex.
```

### Tasks

| Task | Priority | Frequency | Role |
|------|----------|-----------|------|
| Sensor Task | Above Normal | 500ms | Reads BMP280 over I2C, sends data to queue |
| Display Task | Normal | On queue receive | Updates SSD1306 OLED over I2C |
| UART Task | Normal | 1000ms | Reads BMP280, sends formatted data over UART |

### Shared Resources

| Resource | Protection | Why |
|----------|-----------|-----|
| I2C1 bus | osMutex | BMP280 and SSD1306 share the same I2C bus — simultaneous access would corrupt transactions |
| use_fahrenheit flag | volatile | Set by button ISR, read by display and UART tasks — single byte write is atomic on Cortex-M4 |

### Interrupt

The blue user button (PC13) triggers an EXTI interrupt on the falling edge. The ISR toggles the temperature unit flag instantly, regardless of what the RTOS tasks are doing. The display and UART tasks pick up the change on their next cycle.

## Hardware

| Component | Description |
|-----------|-------------|
| NUCLEO-F446RE | STM32 Cortex-M4 development board |
| BMP280 (GY-BMP280) | Temperature and pressure sensor, I2C address 0x76 |
| SSD1306 OLED | 128x64 pixel display, I2C address 0x3C |
| Breadboard + jumper wires | For prototyping connections |

### Wiring

```
BMP280 VCC  → 3.3V
BMP280 GND  → GND
BMP280 SCL  → PB8 (I2C1_SCL)
BMP280 SDA  → PB9 (I2C1_SDA)

SSD1306 VCC → 3.3V
SSD1306 GND → GND
SSD1306 SCL → PB8 (shared with BMP280)
SSD1306 SDA → PB9 (shared with BMP280)

UART TX     → PA2 (USART2_TX, routed through ST-Link VCP)
Blue Button → PC13 (EXTI interrupt, active low)
```

## Toolchain

| Tool | Purpose |
|------|---------|
| VS Code | Code editor |
| STM32CubeMX | Peripheral configuration and code generation |
| ARM GCC (`arm-none-eabi-gcc`) | Cross-compiler for ARM Cortex-M4 |
| CMake + Ninja | Build system |
| OpenOCD | Flashing and debugging over ST-Link SWD |

### Build

```bash
cmake --preset Debug
cmake --build --preset Debug
```

### Flash

```bash
openocd -f interface/stlink.cfg -f target/stm32f4x.cfg -c "program build/Debug/trying_vscode.elf verify reset exit"
```

## UART Output

Connect to the Nucleo's virtual COM port at 115200 baud (8N1):

```
FreeRTOS Weather Station starting...
Temp: 24.69 C  Pressure: 1019.74 hPa
Temp: 24.70 C  Pressure: 1019.78 hPa
Temp: 76.44 F  Pressure: 1019.72 hPa    <-- after button press
Temp: 76.45 F  Pressure: 1019.75 hPa
```

## What I Learned

- FreeRTOS task creation, scheduling, and priority management
- Inter-task communication using message queues
- Shared resource protection using mutexes (I2C bus arbitration)
- Hardware interrupts (EXTI) for responsive user input
- Concurrent system design — separating concerns into independent tasks
- Reading and interpreting IC datasheets (BMP280, SSD1306)
- I2C protocol: addressing, shared bus with multiple devices, register read/write
- Cross-compilation for ARM Cortex-M4 using CMake and arm-none-eabi-gcc
- Flashing and debugging with OpenOCD over SWD

## Project Evolution

This project was originally built as a bare-metal polling loop (see git history). It was then refactored to FreeRTOS to demonstrate RTOS concepts and concurrent task architecture. The git history shows the full evolution from first LED blink to the current multi-task design.

## Author

Built from scratch as a hands-on embedded systems learning project.

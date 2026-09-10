cmake_minimum_required(VERSION 3.22)

# Keep the CubeMX project name so existing flash/debug paths remain valid.
project(trying_vscode LANGUAGES C ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")

add_executable(${CMAKE_PROJECT_NAME})

# CubeMX owns board startup, HAL, and FreeRTOS build configuration.
add_subdirectory(cmake/stm32cubemx)

# Keep handwritten application sources outside the generated source list.
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    Core/Src/bmp280.c
    Core/Src/ssd1306.c
    Core/Src/weather_station.c
)
target_link_libraries(${CMAKE_PROJECT_NAME} stm32cubemx)

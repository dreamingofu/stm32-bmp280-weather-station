cmake_minimum_required(VERSION 3.22)
project(weather_station_host_tests LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

enable_testing()

add_executable(test_bmp280 test_bmp280.c ../Core/Src/bmp280.c)
add_executable(test_ssd1306 test_ssd1306.c ../Core/Src/ssd1306.c)
add_executable(test_weather_station test_weather_station.c ../Core/Src/weather_station.c)
if(APPLE AND CMAKE_C_COMPILER_ID MATCHES "Clang")
    # Apple's host SDK deprecates sprintf; the unchanged firmware uses newlib.
    target_compile_options(test_weather_station PRIVATE -Wno-deprecated-declarations)
endif()

foreach(target IN ITEMS test_bmp280 test_ssd1306 test_weather_station)
    target_include_directories(${target} PRIVATE mocks ../Core/Inc)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${target} PRIVATE -Wall -Wextra -Werror)
    endif()
    add_test(NAME ${target} COMMAND ${target})
endforeach()

target_include_directories(test_weather_station PRIVATE
    ../Middlewares/Third_Party/FreeRTOS/Source/CMSIS_RTOS_V2)

# Task callbacks contain infinite loops; bound failures that miss a mock exit.
set_tests_properties(test_weather_station PROPERTIES TIMEOUT 5)

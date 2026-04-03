#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

/** Single mutex for all TwoWire / I2C access (OLED, mux, BNO055). */
extern SemaphoreHandle_t g_i2cBusMutex;

void i2cBusMutexInit();

struct I2cBusLock {
    I2cBusLock();
    ~I2cBusLock();
    I2cBusLock(const I2cBusLock &) = delete;
    I2cBusLock &operator=(const I2cBusLock &) = delete;
};

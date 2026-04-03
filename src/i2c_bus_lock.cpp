#include "i2c_bus_lock.h"

SemaphoreHandle_t g_i2cBusMutex = nullptr;

void i2cBusMutexInit() {
    if (!g_i2cBusMutex) {
        g_i2cBusMutex = xSemaphoreCreateRecursiveMutex();
    }
}

I2cBusLock::I2cBusLock() {
    if (g_i2cBusMutex) {
        xSemaphoreTakeRecursive(g_i2cBusMutex, portMAX_DELAY);
    }
}

I2cBusLock::~I2cBusLock() {
    if (g_i2cBusMutex) {
        xSemaphoreGiveRecursive(g_i2cBusMutex);
    }
}

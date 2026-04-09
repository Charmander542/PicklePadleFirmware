#include "app_state.h"

QueueHandle_t     g_uiEventQueue = nullptr;
QueueHandle_t     g_netTxQueue   = nullptr;
SemaphoreHandle_t g_stateMutex   = nullptr;

RunMode g_runMode = RunMode::Idle;

void setRunMode(RunMode m) {
    if (xSemaphoreTake(g_stateMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        g_runMode = m;
        xSemaphoreGive(g_stateMutex);
    }
}

RunMode getRunMode() {
    RunMode m = RunMode::Idle;
    if (xSemaphoreTake(g_stateMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        m = g_runMode;
        xSemaphoreGive(g_stateMutex);
    }
    return m;
}

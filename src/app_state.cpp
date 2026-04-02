#include "app_state.h"
#include "app_config.h"
#include <Preferences.h>

QueueHandle_t g_uiEventQueue = nullptr;
QueueHandle_t g_netTxQueue = nullptr;
SemaphoreHandle_t g_stateMutex = nullptr;

RunMode g_runMode = RunMode::Idle;

IPAddress g_hostAddr;
uint16_t g_hostPort = kDefaultHostPort;

bool hostAddrLoadFromPrefs() {
    Preferences p;
    if (!p.begin(kPrefsNamespace, true)) return false;
    String ips = p.getString(kPrefsKeyHostIp, "");
    g_hostPort = p.getUInt(kPrefsKeyHostPort, kDefaultHostPort);
    p.end();
    if (ips.length() == 0) return false;
    return g_hostAddr.fromString(ips);
}

void hostAddrSaveToPrefs(const IPAddress &ip, uint16_t port) {
    Preferences p;
    if (!p.begin(kPrefsNamespace, false)) return;
    p.putString(kPrefsKeyHostIp, ip.toString());
    p.putUInt(kPrefsKeyHostPort, port);
    p.end();
}

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

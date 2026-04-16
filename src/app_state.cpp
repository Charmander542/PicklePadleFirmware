#include "app_state.h"
#include "app_config.h"
#include <Preferences.h>
#include "freertos/portmacro.h"

QueueHandle_t g_uiEventQueue = nullptr;
QueueHandle_t g_netTxQueue = nullptr;
SemaphoreHandle_t g_stateMutex = nullptr;
volatile uint32_t g_netUiEnqueueDrops = 0;

RunMode g_runMode = RunMode::Idle;
static portMUX_TYPE g_runModeMux = portMUX_INITIALIZER_UNLOCKED;

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
    portENTER_CRITICAL(&g_runModeMux);
    g_runMode = m;
    portEXIT_CRITICAL(&g_runModeMux);
}

RunMode getRunMode() {
    portENTER_CRITICAL(&g_runModeMux);
    const RunMode m = g_runMode;
    portEXIT_CRITICAL(&g_runModeMux);
    return m;
}

#include "NetUdp.h"
#include <SdLogger.h>
#include <string.h>

bool NetUdp::begin(uint16_t localPort) {
    localPort_ = localPort;
    sendMu_ = xSemaphoreCreateMutex();
    if (!sendMu_) return false;
    return udp_.begin(localPort_) == 1;
}

void NetUdp::setRemote(const IPAddress &ip, uint16_t port) {
    if (xSemaphoreTake(g_stateMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        g_hostAddr = ip;
        g_hostPort = port;
        xSemaphoreGive(g_stateMutex);
    }
}

bool NetUdp::postText(const char *msg) {
    if (!msg) return false;
    NetOutgoingMsg m{};
    strncpy(m.text, msg, sizeof(m.text) - 1);
    m.text[sizeof(m.text) - 1] = 0;
    return xQueueSend(g_netTxQueue, &m, pdMS_TO_TICKS(20)) == pdTRUE;
}

bool NetUdp::sendNowUnlocked_(const char *msg) {
    if (WiFi.status() != WL_CONNECTED) return false;

    IPAddress ip;
    uint16_t port;
    if (xSemaphoreTake(g_stateMutex, pdMS_TO_TICKS(50)) != pdTRUE) return false;
    ip = g_hostAddr;
    port = g_hostPort;
    xSemaphoreGive(g_stateMutex);

    if (xSemaphoreTake(sendMu_, pdMS_TO_TICKS(200)) != pdTRUE) return false;
    udp_.beginPacket(ip, port);
    udp_.print(msg);
    bool ok = udp_.endPacket() > 0;
    xSemaphoreGive(sendMu_);
    return ok;
}

void NetUdp::service() {
    int n = udp_.parsePacket();
    if (n > 0) {
        char buf[128];
        int r = udp_.read((uint8_t *)buf, sizeof(buf) - 1);
        if (r > 0) {
            buf[r] = 0;
            while (r > 0 && (buf[r - 1] == '\n' || buf[r - 1] == '\r')) buf[--r] = 0;

            if (SdLogger::instance().ok()) SdLogger::instance().logf("udp rx: %s", buf);

            UiEventMsg ev{};
            if (strcasecmp(buf, "swing hit") == 0) {
                ev.kind = UiEvent::SwingHitHost;
                xQueueSend(g_uiEventQueue, &ev, 0);
            } else if (strcasecmp(buf, "idle") == 0) {
                ev.kind = UiEvent::ModeIdle;
                xQueueSend(g_uiEventQueue, &ev, 0);
            } else if (strcasecmp(buf, "gameplay") == 0 || strcasecmp(buf, "game") == 0) {
                ev.kind = UiEvent::ModeGameplay;
                xQueueSend(g_uiEventQueue, &ev, 0);
            }
        }
    }

    NetOutgoingMsg m;
    while (xQueueReceive(g_netTxQueue, &m, 0) == pdTRUE) {
        if (SdLogger::instance().ok()) SdLogger::instance().logf("udp tx: %s", m.text);
        sendNowUnlocked_(m.text);
    }
}

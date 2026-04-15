#include "NetUdp.h"
#include <SdLogger.h>
#include <freertos/task.h>
#include <string.h>

bool NetUdp::begin(uint16_t localPort) {
    localPort_ = localPort;
    udpMu_ = xSemaphoreCreateMutex();
    if (!udpMu_) return false;
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

bool NetUdp::trySendImmediate(const char *msg) {
    if (xSemaphoreTake(udpMu_, pdMS_TO_TICKS(200)) != pdTRUE) return false;
    const bool ok = sendPacketLocked_(msg);
    xSemaphoreGive(udpMu_);
    return ok;
}

bool NetUdp::sendFast(const char *msg, uint16_t len) {
    if (xSemaphoreTake(udpMu_, pdMS_TO_TICKS(2)) != pdTRUE) return false;

    IPAddress ip;
    uint16_t port;
    if (xSemaphoreTake(g_stateMutex, 0) == pdTRUE) {
        ip = g_hostAddr;
        port = g_hostPort;
        xSemaphoreGive(g_stateMutex);
    } else {
        xSemaphoreGive(udpMu_);
        return false;
    }

    bool ok = false;
    if (port != 0 && ip != IPAddress(0, 0, 0, 0)) {
        if (udp_.beginPacket(ip, port)) {
            udp_.write((const uint8_t *)msg, len);
            ok = (udp_.endPacket() > 0);
        }
    }
    xSemaphoreGive(udpMu_);
    return ok;
}

void NetUdp::cacheRemote(IPAddress &outIp, uint16_t &outPort) {
    if (xSemaphoreTake(g_stateMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        outIp = g_hostAddr;
        outPort = g_hostPort;
        xSemaphoreGive(g_stateMutex);
    }
}

bool NetUdp::sendPacketLocked_(const char *msg) {
    if (!msg || msg[0] == 0) return false;
    if (WiFi.status() != WL_CONNECTED) return false;

    IPAddress ip;
    uint16_t port;
    if (xSemaphoreTake(g_stateMutex, pdMS_TO_TICKS(50)) != pdTRUE) return false;
    ip = g_hostAddr;
    port = g_hostPort;
    xSemaphoreGive(g_stateMutex);

    if (port == 0 || ip == IPAddress(0, 0, 0, 0)) return false;

    constexpr int kUdpSendAttempts = 4;
    for (int attempt = 0; attempt < kUdpSendAttempts; ++attempt) {
        if (attempt > 0) {
            vTaskDelay(pdMS_TO_TICKS(2));
        }
        if (!udp_.beginPacket(ip, port)) {
            continue;
        }
        udp_.print(msg);
        if (udp_.endPacket() > 0) {
            return true;
        }
    }
    return false;
}

void NetUdp::service() {
    if (xSemaphoreTake(udpMu_, pdMS_TO_TICKS(50)) != pdTRUE) return;

    const int n = udp_.parsePacket();
    if (n > 0) {
        char buf[128];
        int r = udp_.read((uint8_t *)buf, sizeof(buf) - 1);
        if (r > 0) {
            buf[r] = 0;
            while (r > 0 && (buf[r - 1] == '\n' || buf[r - 1] == '\r')) buf[--r] = 0;

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
            } else if (strcasecmp(buf, "tutorial") == 0) {
                ev.kind = UiEvent::ModeTutorial;
                xQueueSend(g_uiEventQueue, &ev, 0);
            }

            if (SdLogger::instance().ok()) SdLogger::instance().logf("udp rx: %s", buf);
        }
    }

    // Drain a bounded burst per tick so lwIP can free pbufs (errno 12 / ENOMEM if we spam).
    constexpr UBaseType_t kMaxTxPerService = 64;
    for (UBaseType_t tx = 0; tx < kMaxTxPerService; ++tx) {
        NetOutgoingMsg m;
        if (xQueueReceive(g_netTxQueue, &m, 0) != pdTRUE) break;
        sendPacketLocked_(m.text);
    }

    xSemaphoreGive(udpMu_);
}

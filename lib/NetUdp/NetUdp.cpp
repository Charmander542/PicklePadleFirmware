#include "NetUdp.h"
#include "app_config.h"
#include <freertos/task.h>
#include <string.h>
#include <strings.h>

namespace {
bool parseNamedColor(const char *msg, uint8_t &r, uint8_t &g, uint8_t &b) {
    if (!msg || msg[0] == 0) return false;

    const char *name = msg;
    if (strncasecmp(msg, "color ", 6) == 0) {
        name = msg + 6;
    }
    while (*name == ' ') name++;
    if (*name == 0) return false;

    if (strcasecmp(name, "red") == 0) { r = 255; g = 0; b = 0; return true; }
    if (strcasecmp(name, "green") == 0) { r = 0; g = 255; b = 0; return true; }
    if (strcasecmp(name, "blue") == 0) { r = 0; g = 0; b = 255; return true; }
    if (strcasecmp(name, "white") == 0) { r = 255; g = 255; b = 255; return true; }
    if (strcasecmp(name, "yellow") == 0) { r = 255; g = 255; b = 0; return true; }
    if (strcasecmp(name, "purple") == 0 || strcasecmp(name, "magenta") == 0) {
        r = 255; g = 0; b = 255; return true;
    }
    if (strcasecmp(name, "cyan") == 0) { r = 0; g = 255; b = 255; return true; }
    if (strcasecmp(name, "orange") == 0) { r = 255; g = 140; b = 0; return true; }
    if (strcasecmp(name, "off") == 0 || strcasecmp(name, "black") == 0) {
        r = 0; g = 0; b = 0; return true;
    }
    return false;
}

/** Do not block the net task on a full UI queue; drop instead. */
static bool enqueueUiEvent(const UiEventMsg &ev) {
    if (xQueueSend(g_uiEventQueue, &ev, 0) == pdTRUE) {
        return true;
    }
    g_netUiEnqueueDrops++;
    return false;
}

static void trimUdpCmd(char *s) {
    if (!s) return;
    size_t L = strlen(s);
    while (L > 0 && (s[L - 1] == ' ' || s[L - 1] == '\t' || s[L - 1] == '\r' || s[L - 1] == '\n')) {
        s[--L] = '\0';
    }
    char *p = s;
    while (*p == ' ' || *p == '\t') {
        p++;
    }
    if (p != s) {
        memmove(s, p, strlen(p) + 1);
    }
}

/** Read exactly `n` bytes from current WiFiUDP datagram; copy first `cap` into cmd (NUL-terminated). */
static bool readFullDatagram_(WiFiUDP &udp, int n, char *cmd, int cap) {
    if (!cmd || cap < 1 || n <= 0) {
        return false;
    }
    int remain = n;
    int copyLen = 0;
    int safety = n + 400;
    uint8_t slab[128];

    while (remain > 0 && safety-- > 0) {
        const int chunk = (remain > (int)sizeof(slab)) ? (int)sizeof(slab) : remain;
        const int got = udp.read(slab, chunk);
        if (got <= 0) {
            taskYIELD();
            continue;
        }
        if (copyLen < cap - 1) {
            const int take = (got > (cap - 1) - copyLen) ? ((cap - 1) - copyLen) : got;
            memcpy(cmd + copyLen, slab, (size_t)take);
            copyLen += take;
        }
        remain -= got;
    }
    cmd[copyLen] = '\0';
    return (remain == 0);
}

static void dispatchHostLine_(char *line) {
    trimUdpCmd(line);
    if (line[0] == '\0') {
        return;
    }

    UiEventMsg ev{};
    if (strcasecmp(line, "swing hit") == 0) {
        ev.kind = UiEvent::SwingHitHost;
        (void)enqueueUiEvent(ev);
    } else if (strcasecmp(line, "idle") == 0) {
        ev.kind = UiEvent::ModeIdle;
        (void)enqueueUiEvent(ev);
    } else if (strcasecmp(line, "gameplay") == 0 || strcasecmp(line, "game") == 0) {
        ev.kind = UiEvent::ModeGameplay;
        (void)enqueueUiEvent(ev);
    } else if (strcasecmp(line, "tutorial") == 0) {
        ev.kind = UiEvent::ModeTutorial;
        (void)enqueueUiEvent(ev);
    } else {
        uint8_t cr = 0, cg = 0, cb = 0;
        if (parseNamedColor(line, cr, cg, cb)) {
            ev.kind = UiEvent::SetIdleColor;
            ev.r = cr;
            ev.g = cg;
            ev.b = cb;
            (void)enqueueUiEvent(ev);
        }
    }
}
} // namespace

bool NetUdp::begin(uint16_t localPort) {
    localPort_ = localPort;
    return udp_.begin(localPort_) == 1;
}

void NetUdp::setRemote(const IPAddress &ip, uint16_t port) {
    if (xSemaphoreTake(g_stateMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        if (ip != IPAddress(0, 0, 0, 0)) {
            g_hostAddr = ip;
        }
        if (port != 0) {
            g_hostPort = port;
        }
        xSemaphoreGive(g_stateMutex);
    }
}

bool NetUdp::postText(const char *msg) {
    if (!msg) return false;
    NetOutgoingMsg m{};
    strncpy(m.text, msg, sizeof(m.text) - 1);
    m.text[sizeof(m.text) - 1] = 0;
    return xQueueSend(g_netTxQueue, &m, 0) == pdTRUE;
}

void NetUdp::cacheRemote(IPAddress &outIp, uint16_t &outPort) {
    if (xSemaphoreTake(g_stateMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        outIp = g_hostAddr;
        outPort = g_hostPort;
        xSemaphoreGive(g_stateMutex);
    }
}

void NetUdp::rememberRemoteIp_(const IPAddress &ip) {
    if (ip == IPAddress(0, 0, 0, 0)) return;
    setRemote(ip, 0);
}

bool NetUdp::sendPacket_(const char *msg) {
    if (!msg || msg[0] == 0) return false;
    if (WiFi.status() != WL_CONNECTED) return false;

    IPAddress ip;
    uint16_t port;
    if (xSemaphoreTake(g_stateMutex, pdMS_TO_TICKS(50)) != pdTRUE) return false;
    ip = g_hostAddr;
    port = g_hostPort;
    xSemaphoreGive(g_stateMutex);

    if (port == 0 || ip == IPAddress(0, 0, 0, 0)) return false;

    constexpr int kUdpSendAttempts = 2;
    for (int attempt = 0; attempt < kUdpSendAttempts; ++attempt) {
        if (attempt > 0) {
            vTaskDelay(1);
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
    for (unsigned pkt = 0; pkt < kNetUdpRxMaxPerTick; ++pkt) {
        const int n = udp_.parsePacket();
        if (n <= 0) {
            break;
        }

        rememberRemoteIp_(udp_.remoteIP());

        char cmd[160];
        (void)readFullDatagram_(udp_, n, cmd, (int)sizeof(cmd));
        dispatchHostLine_(cmd);
    }

    for (unsigned tx = 0; tx < kNetUdpTxDrainPerTick; ++tx) {
        NetOutgoingMsg m;
        if (xQueueReceive(g_netTxQueue, &m, 0) != pdTRUE) {
            break;
        }
        sendPacket_(m.text);
    }
}

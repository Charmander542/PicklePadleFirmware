#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>  // WiFiUDP class on ESP32
#include "app_state.h"

// One owner for the WiFiUDP socket: the net task services RX and drains queued TX.
// App/UI tasks only enqueue outbound text packets.
class NetUdp {
public:
    bool begin(uint16_t localPort);

    // Safe from any core: copies into queue for net task.
    bool postText(const char *msg);

    // Call only from net task: RX + flush TX queue.
    void service();

    void setRemote(const IPAddress &ip, uint16_t port);

    // Snapshot host addr+port into caller-local storage once (avoids g_stateMutex per packet).
    void cacheRemote(IPAddress &outIp, uint16_t &outPort);

private:
    bool sendPacket_(const char *msg);
    void rememberRemoteIp_(const IPAddress &ip);

    WiFiUDP     udp_;
    uint16_t localPort_{0};
};

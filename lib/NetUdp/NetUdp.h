#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>  // WiFiUDP class on ESP32
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "app_state.h"

// UDP I/O is serviced only from the network task (core 0).
class NetUdp {
public:
    bool begin(uint16_t localPort);

    // Safe from any core: copies into queue for net task.
    bool postText(const char *msg);

    // Call only from net task: RX + flush TX queue.
    void service();

    void setRemote(const IPAddress &ip, uint16_t port);

private:
    bool sendNowUnlocked_(const char *msg);

    WiFiUDP udp_;
    SemaphoreHandle_t sendMu_{nullptr};
    uint16_t localPort_{0};
};

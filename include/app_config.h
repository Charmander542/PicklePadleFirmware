#pragma once

#include <Arduino.h>
#include <IPAddress.h>

// File logging on SPI SD. Some boards hang in SD.begin() with an empty socket or floating MISO.
// Build with -D ENABLE_SD_FILE_LOG=0 in platformio.ini to skip all SD/SPI touch (no boot loop).
#ifndef ENABLE_SD_FILE_LOG
#define ENABLE_SD_FILE_LOG 1
#endif

// Default / fallback Wi‑Fi (open guest networks use empty password).
constexpr char kFallbackSsid[] = "BU Guest (unencrypted)";

// Captive portal AP (when no credentials in NVS).
constexpr char kPortalSsid[] = "PicklePaddle-Setup";
constexpr char kPortalPass[] = "";  // open AP for easier setup

constexpr char kPrefsNamespace[] = "paddle";
constexpr char kPrefsKeySsid[] = "wifi_ssid";
constexpr char kPrefsKeyPass[] = "wifi_pass";
constexpr char kPrefsKeyHostIp[] = "host_ip";
constexpr char kPrefsKeyHostPort[] = "host_port";

constexpr uint16_t kDefaultHostPort = 4210;
constexpr uint16_t kLocalUdpPort = 4211;

// Wi-Fi power shaping: start association at low TX power to reduce brownout risk,
// then raise once connected.
constexpr int8_t kWifiConnectTxPowerDbm = 8;   // low power for association
constexpr int8_t kWifiRunTxPowerDbm = 15;      // normal runtime power
constexpr uint32_t kWifiPowerRampDelayMs = 300;

// BNO055: SA0 low → 0x28, SA0 high → 0x29 (Adafruit “address B”). Your working sketch used 0x29.
constexpr uint8_t kBno055I2cAddr = 0x29;

// IMU I2C clock (Hz) for the bus when not in a dedicated slow read (default / other devices).
constexpr uint32_t kImuI2cHz = 100000;

// Slower clock during BNO055 linear-accel reads — cuts ESP32 Wire error 263 (ESP_ERR_TIMEOUT)
// when WiFi + clock-stretch contend.
constexpr uint32_t kImuReadClockHz = 50000;

// Wire combined write+read timeout (ms) per transaction while reading the IMU.
constexpr uint16_t kImuWireTimeoutMs = 1000;

// IMU / gameplay poll interval (ms). Slower polling eases I2C + WiFi contention while debugging.
constexpr uint32_t kImuPeriodMs = 50;

// Button timing (ms).
constexpr uint32_t kButtonDebounceMs = 45;
constexpr uint32_t kButtonHoldMs = 650;
// Idle only: hold this long to erase saved Wi‑Fi and reboot into setup portal (PicklePaddle-Setup).
constexpr uint32_t kWifiForgetHoldMs = 8000;

// Gameplay: jerk threshold (m/s^3). Production ballpark is often ~1e3–1e4; lowered a lot for bench testing
// (tap the paddle gently). Raise before shipping.
constexpr float kGameplayJerkThreshold = 10.f;
// Minimum ms between transmitted impulses (debounce).
constexpr uint32_t kGameplayJerkRetriggerMs = 350;

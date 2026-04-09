#pragma once

#include <Arduino.h>
#include <IPAddress.h>

// File logging on SPI SD. Some boards hang in SD.begin() with an empty socket or floating MISO.
// Build with -D ENABLE_SD_FILE_LOG=0 in platformio.ini to skip all SD/SPI touch (no boot loop).
#ifndef ENABLE_SD_FILE_LOG
#define ENABLE_SD_FILE_LOG 1
#endif

// Production: skip OLED self-test, skip boot probe UI / IMU sampling / per-motor haptic test; minimal
// hardware init then boot FX only. Use PlatformIO env `esp32dev_prod` or `-D PICKLE_PRODUCTION=1`.
#ifndef PICKLE_PRODUCTION
#define PICKLE_PRODUCTION 0
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
constexpr char kSdLogPath[] = "/PADDLE.LOG";  // 8.3 filename required by current FatFs config

// FreeRTOS: equal priority avoids starving UDP RX (gameplay mode) vs IMU loop.
constexpr uint8_t kAppTaskPriority = 4;
constexpr uint8_t kNetTaskPriority = 4;

// Wi‑Fi power shaping: low TX power during association (reduces peak current / brownout risk), then ramp up.
constexpr int8_t kWifiConnectTxPowerDbm = 2;
// Default STA TX after connect (idle / low duty). Gameplay & tutorial bump to kWifiStreamingTxPowerDbm.
constexpr int8_t kWifiRunTxPowerDbm = 15;
// Max practical STA TX for streaming modes (hardware clamps to chipset max, typically ~19.5 dBm on ESP32).
constexpr int8_t kWifiStreamingTxPowerDbm = 19;
constexpr uint32_t kWifiPowerRampStepDelayMs = 700;

// BNO055: SA0 low → 0x28, SA0 high → 0x29 (Adafruit “address B”). Your working sketch used 0x29.
constexpr uint8_t kBno055I2cAddr = 0x29;

// IMU I2C clock (Hz) for the bus when not in a dedicated slow read (default / other devices).
constexpr uint32_t kImuI2cHz = 100000;

// Slower clock during BNO055 linear-accel reads — cuts ESP32 Wire error 263 (ESP_ERR_TIMEOUT)
// when WiFi + clock-stretch contend.
constexpr uint32_t kImuReadClockHz = 50000;

// Wire combined write+read timeout (ms) per transaction while reading the IMU.
constexpr uint16_t kImuWireTimeoutMs = 1000;

// IMU poll intervals (ms).
constexpr uint32_t kGameplayImuPeriodMs = 20;  // faster sampling → quicker impulse detection (was 50).
constexpr uint32_t kTutorialImuPeriodMs = 12;  // much faster stream for tuning/capture

// Button timing (ms).
constexpr uint32_t kButtonDebounceMs = 45;
constexpr uint32_t kButtonHoldMs = 650;
// Idle only: hold this long to erase saved Wi‑Fi and reboot into setup portal (PicklePaddle-Setup).
constexpr uint32_t kWifiForgetHoldMs = 8000;

// Gameplay: jerk threshold (m/s^3). Production ballpark is often ~1e3–1e4; lowered a lot for bench testing
// (tap the paddle gently). Raise before shipping.
constexpr float kGameplayJerkThreshold = 35.f;
// Minimum ms between transmitted impulses (debounce).
constexpr uint32_t kGameplayJerkRetriggerMs = 1000;
// Higher = IMU filter tracks sudden accel faster (0.01–1). Gameplay uses a snappier filter than before (0.2).
constexpr float kGameplayJerkLpfAlpha = 0.42f;

// Tutorial streams IMU faster (kTutorialImuPeriodMs): shorter Δt makes |Δa|/Δt smaller per step with the
// same LPF, so use a more responsive filter, lower threshold, and shorter re-arm for the CSV impulse column.
constexpr float kTutorialJerkThreshold = 8.f;
constexpr uint32_t kTutorialJerkRetriggerMs = 120;
constexpr float kTutorialJerkLpfAlpha = 0.45f;

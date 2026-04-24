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

// Net slightly above app so UDP RX / command enqueue runs under IMU+sendFast load.
constexpr uint8_t kAppTaskPriority = 4;
constexpr uint8_t kNetTaskPriority = 5;
// Host “swing hit” LED + haptic worker: below app/net so gameplay IMU + UDP stay responsive.
constexpr uint8_t kSwingFxTaskPriority = 3;
constexpr uint32_t kSwingFxTaskStackBytes = 4096;
constexpr unsigned kSwingFxQueueDepth = 4;

// UI events from UDP / net task — must not drop mode changes when app task is busy (tutorial flood).
constexpr unsigned kUiEventQueueDepth = 128;

// Cap TX drain per service() tick so outbound bursts do not monopolize command receive.
constexpr unsigned kNetUdpTxDrainPerTick = 12;
// Hard cap on RX datagrams per service() tick — avoids starving TX if the host floods UDP.
constexpr unsigned kNetUdpRxMaxPerTick = 24;

// Consecutive sendPacket_ failures before the net task tears down and re-opens the UDP socket.
// Handles silent socket corruption after WiFi radio reconfiguration on some ESP32 modules.
constexpr unsigned kNetSendFailReinitThreshold = 6;

// Wi‑Fi power shaping: low TX power during association (reduces peak current / brownout risk), then ramp up.
constexpr int8_t kWifiConnectTxPowerDbm = 2;
// Default STA TX after connect (idle / low duty). Gameplay & tutorial bump to kWifiStreamingTxPowerDbm.
constexpr int8_t kWifiRunTxPowerDbm = 15;
// Max practical STA TX for streaming modes (hardware clamps to chipset max, typically ~19.5 dBm on ESP32).
constexpr int8_t kWifiStreamingTxPowerDbm = 19;

// Milliseconds between each STA TX-power step after association (keep small; full seconds of ramp
// was easy to mis-trigger as “WiFi broken” and delays boot unacceptably).
constexpr uint32_t kWifiTxPowerRampStepMs = 60;
// Short pause after DHCP before applying idle radio (modem sleep off, run TX); brownout cushion.
constexpr uint32_t kWifiPostConnectSettleMs = 280;

// BNO055: SA0 low → 0x28, SA0 high → 0x29 (Adafruit “address B”). Your working sketch used 0x29.
constexpr uint8_t kBno055I2cAddr = 0x29;

// IMU I2C clock (Hz) for the bus when not in a dedicated slow read (default / other devices).
constexpr uint32_t kImuI2cHz = 100000;

// Slower clock during BNO055 linear-accel reads — cuts ESP32 Wire error 263 (ESP_ERR_TIMEOUT)
// when WiFi + clock-stretch contend.
constexpr uint32_t kImuReadClockHz = 50000;

// Fast clock for tutorial streaming. BNO055 supports 400 kHz; use it when flooding UDP.
constexpr uint32_t kImuFastClockHz = 400000;

// Wire combined write+read timeout (ms) per transaction while reading the IMU.
constexpr uint16_t kImuWireTimeoutMs = 1000;
// Tight timeout for the tutorial flood path (single attempt; move on if bus hiccups).
constexpr uint16_t kImuFastWireTimeoutMs = 50;

// IMU poll intervals (ms).
constexpr uint32_t kGameplayImuPeriodMs = 5;  // tighter impulse latency in gameplay.
// Tutorial stream pacing: keep very fast, but not absolute max flood.
constexpr uint32_t kTutorialImuPeriodMs = 10;

// Button timing (ms).
constexpr uint32_t kButtonDebounceMs = 12;
constexpr uint32_t kButtonHoldMs = 650;
// Idle only: hold this long to erase saved Wi‑Fi and reboot into setup portal (PicklePaddle-Setup).
constexpr uint32_t kWifiForgetHoldMs = 5000;

// Gameplay: JerkDetector |Δa_filt|/Δt threshold (m/s^3). Tune like tutorial impulse; higher = harder swing to fire.
constexpr float kGameplayJerkThreshold = 500.f;
// Minimum ms between gameplay jerk-trigger UDP sends (debounce / re-arm).
constexpr uint32_t kGameplayJerkRetriggerMs = 800;

// LPF on linear accel before jerk (0.01–1); higher = faster tracking of sudden accel.
constexpr float kGameplayJerkLpfAlpha = 0.42f;

// Tutorial streams IMU faster (kTutorialImuPeriodMs): shorter Δt makes |Δa|/Δt smaller per step with the
// same LPF, so use a more responsive filter, lower threshold, and shorter re-arm for the CSV impulse column.
// Lower threshold and increase LPF responsiveness so tutorial flood impulses
// still cross the threshold at higher sampling / TX rates.
constexpr float kTutorialJerkThreshold = 4.0f;
constexpr uint32_t kTutorialJerkRetriggerMs = 60;
constexpr float kTutorialJerkLpfAlpha = 0.75f;

#pragma once

#include <Arduino.h>
#include <IPAddress.h>

// Default / fallback Wi‑Fi (open guest networks use empty password).
constexpr char kFallbackSsid[] = "BU Guest";

// Captive portal AP (when no credentials in NVS).
constexpr char kPortalSsid[] = "PicklePaddel-Setup";
constexpr char kPortalPass[] = "";  // open AP for easier setup

constexpr char kPrefsNamespace[] = "paddle";
constexpr char kPrefsKeySsid[] = "wifi_ssid";
constexpr char kPrefsKeyPass[] = "wifi_pass";
constexpr char kPrefsKeyHostIp[] = "host_ip";
constexpr char kPrefsKeyHostPort[] = "host_port";

constexpr uint16_t kDefaultHostPort = 4210;
constexpr uint16_t kLocalUdpPort = 4211;

// IMU / gameplay sampling (app core loop timing).
constexpr uint32_t kImuPeriodMs = 20;  // 50 Hz

// Button timing (ms).
constexpr uint32_t kButtonDebounceMs = 45;
constexpr uint32_t kButtonHoldMs = 650;

// Gameplay: jerk transmission (reuse jerk_detect thresholds in main if needed).
constexpr float kGameplayJerkThreshold = 3500.f;
constexpr uint32_t kGameplayJerkRetriggerMs = 180;

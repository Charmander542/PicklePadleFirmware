#pragma once

#include <Arduino.h>

// ─── Addressing ───────────────────────────────────────────────────────────────

// Maximum number of individually addressable paddle nodes (IDs 0 to kMaxNodes-1).
// Increase if you have more than 32 paddles on a single central node.
constexpr uint8_t kMaxNodes = 32;

// Node ID used in packets originating from the central node.
constexpr uint8_t kCentralNodeId = 0xFF;

// ─── NVS ─────────────────────────────────────────────────────────────────────

// NVS namespace for central-node persistent state (registered peer MAC table).
constexpr char kCnPrefsNamespace[] = "cn";

// ─── On-air packet (must match lib/EspNowNet/EspNowNet.h) ────────────────────

#define ESPNOW_TEXT_MAX 94

#pragma pack(push, 1)
struct EspNowPacket {
    uint8_t src_id;
    uint8_t dst_id;
    char    text[ESPNOW_TEXT_MAX];
};
#pragma pack(pop)

static_assert(sizeof(EspNowPacket) <= 250, "EspNowPacket exceeds ESP-NOW payload limit");

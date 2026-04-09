#pragma once

#include <Arduino.h>
#include <esp_now.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

// Maximum text payload length including null terminator.
#define ESPNOW_TEXT_MAX 94

/**
 * On-air packet shared between nodes and the central node.
 *
 *   src_id  – sender's node_id (0-254; 0xFF = central).
 *   dst_id  – intended recipient's node_id (0-254; 0xFF = broadcast to all nodes).
 *   text    – null-terminated message string.
 *
 * Total size = 96 bytes (well within ESP-NOW's 250-byte limit).
 */
#pragma pack(push, 1)
struct EspNowPacket {
    uint8_t src_id;
    uint8_t dst_id;
    char    text[ESPNOW_TEXT_MAX];
};
#pragma pack(pop)

static_assert(sizeof(EspNowPacket) <= 250, "EspNowPacket exceeds ESP-NOW payload limit");

/**
 * Thin ESP-NOW transport layer for PicklePaddle nodes.
 *
 * Topology: star. Every node sends to the broadcast address so the central
 * node receives all traffic without needing each node's MAC up front. The
 * central node likewise broadcasts commands; nodes filter on dst_id.
 *
 * Usage (from setup()):
 *   gNet.begin(nodeId);
 *
 * Sending (any task):
 *   gNet.postText("25.3");   // thread-safe; enqueues for the network task
 *
 * Draining (network task only):
 *   gNet.service();           // called in a tight loop on core 0
 */
class EspNowNet {
public:
    /**
     * Initialise ESP-NOW. Sets WiFi to STA mode (no AP connection) and
     * registers broadcast peer + send/recv callbacks.
     *
     * @param nodeId  This paddle's address (0-254; stored in NVS by caller).
     * @return true on success.
     */
    bool begin(uint8_t nodeId);

    /**
     * Thread-safe enqueue of a text message destined for the central node.
     * Copies msg into the TX queue; returns false if the queue is full.
     */
    bool postText(const char *msg);

    /**
     * Drain the TX queue and send pending frames. Must be called only from
     * the network task (core 0); not reentrant.
     */
    void service();

    uint8_t nodeId() const { return nodeId_; }

private:
    static void onSend_(const uint8_t *mac, esp_now_send_status_t status);
    static void onRecv_(const uint8_t *mac, const uint8_t *data, int len);

    static EspNowNet *s_instance_;

    uint8_t nodeId_{0};
};

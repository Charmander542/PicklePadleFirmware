/**
 * PicklePaddle Central Node — ESP-NOW Star Topology Bridge
 *
 * Role: single hub that sits between a PC (USB serial) and up to kMaxNodes
 *       PicklePaddle paddle nodes communicating over ESP-NOW.
 *
 * Serial protocol (PC ↔ Central, 115200 baud, newline-terminated):
 *
 *   PC → Central:
 *     <id>:<message>          Send <message> to the paddle with address <id>.
 *     *:<message>             Broadcast <message> to every registered paddle.
 *     255:<message>           Same as * (0xFF broadcast).
 *     reg <id> AA:BB:CC:EE:FF Register a paddle's MAC for unicast delivery.
 *                             Persisted to NVS; survives power-cycle.
 *     unreg <id>              Remove a paddle from the peer table + NVS.
 *     list                    Print all registered nodes and their MACs.
 *     mac                     Print the central node's own MAC address.
 *
 *   Central → PC:
 *     <id>:<message>          Data (impulse, CSV row, button event, …) from paddle <id>.
 *     [cn] …                  Informational / error messages from the central node.
 *
 * Auto-registration: when a paddle sends data for the first time the central
 * node automatically adds that paddle's MAC to the peer table and prints a
 * [cn] line to the PC. This means paddles do not need to be manually registered
 * before they start sending; manual `reg` is only required when the central
 * needs to *send* to a paddle before it has heard from it.
 */

#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <Preferences.h>
#include <string.h>
#include <stdio.h>
#include "cn_config.h"

// ─── Peer table ───────────────────────────────────────────────────────────────

static uint8_t s_peerMac[kMaxNodes][6];
static bool    s_peerValid[kMaxNodes];

// ─── NVS helpers ─────────────────────────────────────────────────────────────

static void nvsSavePeer(uint8_t id) {
    char key[10];
    snprintf(key, sizeof(key), "mac%u", (unsigned)id);
    Preferences prefs;
    prefs.begin(kCnPrefsNamespace, false);
    prefs.putBytes(key, s_peerMac[id], 6);
    prefs.end();
}

static void nvsDeletePeer(uint8_t id) {
    char key[10];
    snprintf(key, sizeof(key), "mac%u", (unsigned)id);
    Preferences prefs;
    prefs.begin(kCnPrefsNamespace, false);
    prefs.remove(key);
    prefs.end();
}

static void nvsLoadAll() {
    Preferences prefs;
    prefs.begin(kCnPrefsNamespace, true);
    for (uint8_t i = 0; i < kMaxNodes; i++) {
        char key[10];
        snprintf(key, sizeof(key), "mac%u", (unsigned)i);
        if (prefs.getBytesLength(key) == 6) {
            prefs.getBytes(key, s_peerMac[i], 6);
            s_peerValid[i] = true;
        }
    }
    prefs.end();
}

// ─── Peer management ─────────────────────────────────────────────────────────

static bool espnowAddPeer(const uint8_t mac[6]) {
    if (esp_now_is_peer_exist(mac)) return true;
    esp_now_peer_info_t peer{};
    memcpy(peer.peer_addr, mac, 6);
    peer.channel = 0;
    peer.encrypt = false;
    return esp_now_add_peer(&peer) == ESP_OK;
}

static bool registerPeer(uint8_t id, const uint8_t mac[6], bool persist) {
    if (id >= kMaxNodes) {
        Serial.printf("[cn] node id %u out of range (max %u)\n",
                      (unsigned)id, (unsigned)(kMaxNodes - 1));
        return false;
    }

    // Remove stale ESP-NOW entry if MAC changed.
    if (s_peerValid[id] && memcmp(s_peerMac[id], mac, 6) != 0) {
        esp_now_del_peer(s_peerMac[id]);
    }

    memcpy(s_peerMac[id], mac, 6);
    s_peerValid[id] = true;

    if (!espnowAddPeer(mac)) {
        Serial.printf("[cn] esp_now_add_peer for node %u failed\n", (unsigned)id);
    }

    if (persist) nvsSavePeer(id);

    Serial.printf("[cn] registered node %u = %02X:%02X:%02X:%02X:%02X:%02X\n",
                  (unsigned)id,
                  mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return true;
}

static void unregisterPeer(uint8_t id) {
    if (id >= kMaxNodes || !s_peerValid[id]) {
        Serial.printf("[cn] node %u not registered\n", (unsigned)id);
        return;
    }
    esp_now_del_peer(s_peerMac[id]);
    memset(s_peerMac[id], 0, 6);
    s_peerValid[id] = false;
    nvsDeletePeer(id);
    Serial.printf("[cn] unregistered node %u\n", (unsigned)id);
}

// node 0 = E0:8C:FE:91:09:3C
// ─── ESP-NOW callbacks ────────────────────────────────────────────────────────

static void onSend(const uint8_t * /*mac*/, esp_now_send_status_t status) {
    if (status != ESP_NOW_SEND_SUCCESS) {
        Serial.printf("[cn] send FAIL (status=%d)\n", (int)status);
    }
}

static void onRecv(const uint8_t *mac, const uint8_t *data, int len) {
    if (static_cast<size_t>(len) < sizeof(EspNowPacket)) return;

    const EspNowPacket *pkt = reinterpret_cast<const EspNowPacket *>(data);

    char text[ESPNOW_TEXT_MAX];
    strncpy(text, pkt->text, ESPNOW_TEXT_MAX - 1);
    text[ESPNOW_TEXT_MAX - 1] = '\0';

    // Strip trailing whitespace.
    int r = static_cast<int>(strlen(text));
    while (r > 0 && (text[r - 1] == '\n' || text[r - 1] == '\r')) text[--r] = '\0';

    const uint8_t sid = pkt->src_id;

    // Auto-register previously unseen nodes so the central can send back to them.
    if (sid < kMaxNodes && !s_peerValid[sid]) {
        Serial.printf("[cn] auto-registering new node %u\n", (unsigned)sid);
        registerPeer(sid, mac, true);
    }

    // Forward payload to PC.
    Serial.printf("%u:%s\n", (unsigned)sid, text);
}

// ─── Serial command parser ────────────────────────────────────────────────────

static bool parseMac(const char *str, uint8_t out[6]) {
    unsigned v[6] = {};
    if (sscanf(str, "%x:%x:%x:%x:%x:%x",
               &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]) != 6) return false;
    for (int i = 0; i < 6; i++) out[i] = static_cast<uint8_t>(v[i]);
    return true;
}

static String s_serialBuf;

static void processLine(const String &line) {
    // ── "mac" ────────────────────────────────────────────────────────────────
    if (line == "mac") {
        Serial.printf("[cn] Central MAC: %s\n", WiFi.macAddress().c_str());
        return;
    }

    // ── "list" ───────────────────────────────────────────────────────────────
    if (line == "list") {
        uint8_t n = 0;
        for (uint8_t i = 0; i < kMaxNodes; i++) {
            if (!s_peerValid[i]) continue;
            Serial.printf("[cn] node %u = %02X:%02X:%02X:%02X:%02X:%02X\n",
                          (unsigned)i,
                          s_peerMac[i][0], s_peerMac[i][1], s_peerMac[i][2],
                          s_peerMac[i][3], s_peerMac[i][4], s_peerMac[i][5]);
            n++;
        }
        if (n == 0) Serial.println("[cn] no nodes registered");
        return;
    }

    // ── "reg <id> AA:BB:CC:DD:EE:FF" ─────────────────────────────────────────
    if (line.startsWith("reg ")) {
        // Find the space between id and mac.
        int sp = line.indexOf(' ', 4);
        if (sp < 0) {
            Serial.println("[cn] usage: reg <id> AA:BB:CC:DD:EE:FF");
            return;
        }
        const uint8_t id = static_cast<uint8_t>(line.substring(4, sp).toInt());
        uint8_t mac[6];
        if (!parseMac(line.substring(sp + 1).c_str(), mac)) {
            Serial.println("[cn] invalid MAC — expected AA:BB:CC:DD:EE:FF");
            return;
        }
        registerPeer(id, mac, true);
        return;
    }

    // ── "unreg <id>" ──────────────────────────────────────────────────────────
    if (line.startsWith("unreg ")) {
        const uint8_t id = static_cast<uint8_t>(line.substring(6).toInt());
        unregisterPeer(id);
        return;
    }

    // ── "<id>:<message>" or "*:<message>" ─────────────────────────────────────
    const int colonPos = line.indexOf(':');
    if (colonPos < 0) {
        Serial.println("[cn] unknown command. Valid: <id>:<msg>  *:<msg>  reg  unreg  list  mac");
        return;
    }

    const String idStr = line.substring(0, colonPos);
    const String msg   = line.substring(colonPos + 1);

    EspNowPacket pkt{};
    pkt.src_id = kCentralNodeId;
    strncpy(pkt.text, msg.c_str(), ESPNOW_TEXT_MAX - 1);
    pkt.text[ESPNOW_TEXT_MAX - 1] = '\0';

    const bool broadcast = (idStr == "*" || idStr == "255");

    if (broadcast) {
        // Send unicast to each registered node (more reliable than true broadcast in ESP-NOW).
        pkt.dst_id = 0xFF;
        bool sent = false;
        for (uint8_t i = 0; i < kMaxNodes; i++) {
            if (!s_peerValid[i]) continue;
            pkt.dst_id = i;
            esp_now_send(s_peerMac[i], reinterpret_cast<const uint8_t *>(&pkt), sizeof(pkt));
            sent = true;
        }
        if (!sent) Serial.println("[cn] no registered nodes — nothing sent");
    } else {
        const uint8_t id = static_cast<uint8_t>(idStr.toInt());
        if (id >= kMaxNodes || !s_peerValid[id]) {
            Serial.printf("[cn] node %u not registered; use: reg %u AA:BB:CC:DD:EE:FF\n",
                          (unsigned)id, (unsigned)id);
            return;
        }
        pkt.dst_id = id;
        esp_now_send(s_peerMac[id], reinterpret_cast<const uint8_t *>(&pkt), sizeof(pkt));
    }
}

static void processSerial() {
    while (Serial.available()) {
        const char c = static_cast<char>(Serial.read());
        if (c == '\n' || c == '\r') {
            if (s_serialBuf.length() > 0) {
                processLine(s_serialBuf);
                s_serialBuf = "";
            }
        } else {
            if (s_serialBuf.length() < 256) s_serialBuf += c;
        }
    }
}

// ─── Setup / Loop ─────────────────────────────────────────────────────────────

void setup() {
    Serial.begin(115200);
    delay(200);

    memset(s_peerMac,   0, sizeof(s_peerMac));
    memset(s_peerValid, 0, sizeof(s_peerValid));

    // Load persisted peer table from NVS before ESP-NOW init.
    nvsLoadAll();

    // Initialise ESP-NOW — WiFi STA mode, no AP connection.
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    // Disable modem sleep for lower command latency.
    if (esp_wifi_set_ps(WIFI_PS_NONE) != ESP_OK) {
        Serial.println("[cn] failed to disable WiFi power save");
    }

    if (esp_now_init() != ESP_OK) {
        Serial.println("[cn] ESP-NOW init FAILED — halting.");
        for (;;) delay(1000);
    }

    esp_now_register_send_cb(onSend);
    esp_now_register_recv_cb(onRecv);

    // Re-add all persisted peers to the ESP-NOW peer table.
    uint8_t loaded = 0;
    for (uint8_t i = 0; i < kMaxNodes; i++) {
        if (!s_peerValid[i]) continue;
        espnowAddPeer(s_peerMac[i]);
        loaded++;
    }

    Serial.printf("[cn] Central MAC: %s\n", WiFi.macAddress().c_str());
    Serial.printf("[cn] Ready. %u node(s) loaded from NVS.\n", (unsigned)loaded);
    Serial.println("[cn] Commands: <id>:<msg>  *:<msg>  reg <id> <mac>  unreg <id>  list  mac");
}

void loop() {
    processSerial();
    delay(2);
}

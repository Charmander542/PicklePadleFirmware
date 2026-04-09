#include "EspNowNet.h"

#include <WiFi.h>
#include <esp_wifi.h>
#include <SdLogger.h>
#include <string.h>
#include <freertos/task.h>
#include "app_state.h"

// Both nodes and central use the broadcast address so no MAC provisioning is
// required for initial bring-up. Application-level dst_id filtering ensures
// only the intended recipient acts on each message.
static const uint8_t kBroadcastMac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

EspNowNet *EspNowNet::s_instance_ = nullptr;

bool EspNowNet::begin(uint8_t nodeId) {
    nodeId_     = nodeId;
    s_instance_ = this;

    // ESP-NOW requires the Wi-Fi radio to be up, but we never associate to an AP.
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    // Disable modem sleep to reduce ESP-NOW command latency.
    if (esp_wifi_set_ps(WIFI_PS_NONE) != ESP_OK) {
        SdLogger::serialPrintln("[espnow] failed to disable WiFi power save");
    }

    if (esp_now_init() != ESP_OK) {
        SdLogger::serialPrintln("[espnow] init FAILED");
        return false;
    }

    esp_now_register_send_cb(onSend_);
    esp_now_register_recv_cb(onRecv_);

    // Register the broadcast peer so esp_now_send() accepts FF:FF:FF:FF:FF:FF.
    esp_now_peer_info_t peer{};
    memcpy(peer.peer_addr, kBroadcastMac, 6);
    peer.channel = 0;      // 0 = follow current Wi-Fi channel (channel 1 when not associated)
    peer.encrypt = false;
    if (esp_now_add_peer(&peer) != ESP_OK) {
        SdLogger::serialPrintln("[espnow] add broadcast peer FAILED");
        return false;
    }

    SdLogger::serialPrintf("[espnow] ready, node_id=%u  mac=%s\n",
                           (unsigned)nodeId_, WiFi.macAddress().c_str());
    return true;
}

bool EspNowNet::postText(const char *msg) {
    if (!msg) return false;
    NetOutgoingMsg m{};
    strncpy(m.text, msg, sizeof(m.text) - 1);
    m.text[sizeof(m.text) - 1] = '\0';
    return xQueueSend(g_netTxQueue, &m, pdMS_TO_TICKS(20)) == pdTRUE;
}

void EspNowNet::service() {
    // Drain a bounded burst per tick to avoid starving the ESP-NOW send pipeline.
    // Tutorial mode produces ~83 rows/s; 12 msgs/tick at 2ms cadence gives plenty of headroom.
    constexpr UBaseType_t kMaxTxPerService = 12;

    for (UBaseType_t tx = 0; tx < kMaxTxPerService; ++tx) {
        NetOutgoingMsg m;
        if (xQueueReceive(g_netTxQueue, &m, 0) != pdTRUE) break;

        EspNowPacket pkt{};
        pkt.src_id = nodeId_;
        pkt.dst_id = 0xFF;  // central node accepts all src_ids
        strncpy(pkt.text, m.text, ESPNOW_TEXT_MAX - 1);
        pkt.text[ESPNOW_TEXT_MAX - 1] = '\0';

        const esp_err_t err = esp_now_send(kBroadcastMac,
                                           reinterpret_cast<const uint8_t *>(&pkt),
                                           sizeof(pkt));
        if (err != ESP_OK) {
            SdLogger::serialPrintf("[espnow] send err %d\n", (int)err);
        } else if (SdLogger::instance().ok()) {
            SdLogger::instance().logf("espnow tx: %s", m.text);
        }
    }
}

// Called from the ESP-NOW/Wi-Fi task — not a hardware ISR, so xQueueSend is safe.
void EspNowNet::onSend_(const uint8_t * /*mac*/, esp_now_send_status_t status) {
    if (status != ESP_NOW_SEND_SUCCESS) {
        SdLogger::serialPrintf("[espnow] send cb: FAIL (%d)\n", (int)status);
    }
}

void EspNowNet::onRecv_(const uint8_t * /*mac*/, const uint8_t *data, int len) {
    if (!s_instance_) return;
    if (static_cast<size_t>(len) < sizeof(EspNowPacket)) return;

    const EspNowPacket *pkt = reinterpret_cast<const EspNowPacket *>(data);

    // Accept only messages addressed to this node or broadcast.
    if (pkt->dst_id != s_instance_->nodeId_ && pkt->dst_id != 0xFF) return;

    char buf[ESPNOW_TEXT_MAX];
    strncpy(buf, pkt->text, ESPNOW_TEXT_MAX - 1);
    buf[ESPNOW_TEXT_MAX - 1] = '\0';

    // Strip trailing CR/LF.
    int r = static_cast<int>(strlen(buf));
    while (r > 0 && (buf[r - 1] == '\n' || buf[r - 1] == '\r')) buf[--r] = '\0';

    UiEventMsg ev{};
    if (strcasecmp(buf, "swing hit") == 0) {
        ev.kind = UiEvent::SwingHitHost;
        xQueueSend(g_uiEventQueue, &ev, 0);
        return;
    } else if (strcasecmp(buf, "idle") == 0) {
        ev.kind = UiEvent::ModeIdle;
        xQueueSend(g_uiEventQueue, &ev, 0);
        return;
    } else if (strcasecmp(buf, "gameplay") == 0 || strcasecmp(buf, "game") == 0) {
        ev.kind = UiEvent::ModeGameplay;
        xQueueSend(g_uiEventQueue, &ev, 0);
        return;
    } else if (strcasecmp(buf, "tutorial") == 0) {
        ev.kind = UiEvent::ModeTutorial;
        xQueueSend(g_uiEventQueue, &ev, 0);
        return;
    }

    if (SdLogger::instance().ok()) SdLogger::instance().logf("espnow rx: %s", buf);
}

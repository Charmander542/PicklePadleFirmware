#include "NeoPixelStrip.h"
#include "paddle_fx_led.h"

namespace {

constexpr uint32_t kWifiAnimIntervalMs = 75;

} // namespace

bool NeoPixelStrip::begin() {
    strip_.begin();
    strip_.setBrightness(PADDLEFX_NEO_BRIGHTNESS);
    strip_.clear();
    strip_.show();
    return true;
}

void NeoPixelStrip::clear() {
    strip_.clear();
    strip_.show();
}

void NeoPixelStrip::playBootSequence() { fxNeoBootChase(strip_); }

void NeoPixelStrip::playBallHit() { fxNeoBallHit(strip_); }

void NeoPixelStrip::resetWifiLedAnim() {
    wifiAnimLastMs_ = 0;
    wifiPingPos_ = 0;
    wifiPingDir_ = 1;
}

void NeoPixelStrip::wifiPingPongFrame_() {
    const uint32_t now = millis();
    if (wifiAnimLastMs_ && (now - wifiAnimLastMs_) < kWifiAnimIntervalMs) {
        return;
    }
    wifiAnimLastMs_ = now;

    const int16_t n = (int16_t)strip_.numPixels();
    if (n <= 0) {
        return;
    }

    strip_.clear();
    if (n == 1) {
        strip_.setPixelColor(0, strip_.Color(0, 90, 140));
        strip_.show();
        return;
    }

    const int head = wifiPingPos_;
    strip_.setPixelColor(head, strip_.Color(0, 110, 180));
    if (head > 0) {
        strip_.setPixelColor(head - 1, strip_.Color(0, 50, 90));
    }
    if (head < n - 1) {
        strip_.setPixelColor(head + 1, strip_.Color(0, 50, 90));
    }

    wifiPingPos_ += wifiPingDir_;
    if (wifiPingPos_ >= n - 1) {
        wifiPingPos_ = n - 1;
        wifiPingDir_ = -1;
    } else if (wifiPingPos_ <= 0) {
        wifiPingPos_ = 0;
        wifiPingDir_ = 1;
    }

    strip_.show();
}

void NeoPixelStrip::tickApPortal(bool stationConnected) {
    if (!stationConnected) {
        resetWifiLedAnim();
        strip_.clear();
        strip_.show();
        return;
    }
    wifiPingPongFrame_();
}

void NeoPixelStrip::tickStaConnecting() {
    wifiPingPongFrame_();
}

void NeoPixelStrip::showStaConnectedSolid() {
    resetWifiLedAnim();
    const uint16_t n = strip_.numPixels();
    const uint32_t c = strip_.Color(0, 70, 0);
    for (uint16_t i = 0; i < n; i++) {
        strip_.setPixelColor(i, c);
    }
    strip_.show();
}

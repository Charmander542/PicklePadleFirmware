#pragma once

#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include "pins.h"
#include "paddle_fx_config.h"

#ifndef PADDLEFX_NEO_ORDER
#define PADDLEFX_NEO_ORDER NEO_GRB
#endif

class NeoPixelStrip {
public:
    NeoPixelStrip()
        : strip_(PADDLEFX_NEO_COUNT, NEOPIXEL_PIN, PADDLEFX_NEO_ORDER) {}

    bool begin();
    void clear();
    void playBootSequence();
    void playBallHit();

    /** Setup AP: back-and-forth “loading” while a phone/PC is joined; idle (off) when none. */
    void tickApPortal(bool stationConnected);
    /** STA: ping-pong while associating to router. */
    void tickStaConnecting();
    /** STA: steady fill after WiFi is up. */
    void showStaConnectedSolid();
    /** Force a solid RGB color immediately. */
    void showSolidColor(uint8_t r, uint8_t g, uint8_t b);
    void resetWifiLedAnim();

    Adafruit_NeoPixel &raw() { return strip_; }

private:
    void wifiPingPongFrame_();

    Adafruit_NeoPixel strip_;
    uint32_t wifiAnimLastMs_{0};
    int16_t wifiPingPos_{0};
    int8_t wifiPingDir_{1};
};

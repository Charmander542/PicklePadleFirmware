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

    Adafruit_NeoPixel &raw() { return strip_; }

private:
    Adafruit_NeoPixel strip_;
};

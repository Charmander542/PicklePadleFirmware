// NeoPixel visuals — included by NeoPixelStrip (no DRV2605 dependency).

#pragma once

#include <Arduino.h>
#include <Adafruit_NeoPixel.h>

inline uint32_t fxColor(uint8_t r, uint8_t g, uint8_t b) {
    return (uint32_t)r << 16 | (uint32_t)g << 8 | b;
}

inline void fxClear(Adafruit_NeoPixel &strip) {
    strip.clear();
    strip.show();
}

inline void fxNeoBootChase(Adafruit_NeoPixel &strip) {
    const uint32_t c = fxColor(0, 80, 40);
    for (int i = 0; i < (int)strip.numPixels(); i++) {
        strip.clear();
        strip.setPixelColor(i, c);
        strip.show();
        delay(35);
    }
    strip.clear();
    strip.show();
}

inline void fxNeoBallHit(Adafruit_NeoPixel &strip) {
    for (uint16_t i = 0; i < strip.numPixels(); i++)
        strip.setPixelColor(i, fxColor(64, 64, 200));
    strip.show();
    delay(80);
    strip.clear();
    strip.show();
}

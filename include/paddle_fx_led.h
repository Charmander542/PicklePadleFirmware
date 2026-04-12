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
    const uint16_t numPixels = strip.numPixels();
    const uint32_t purple_color = fxColor(128, 0, 128);  // Purple color
    const int steps = 50;  // Number of fade steps
    const int delayMs = 1000 / (steps * 2);  // Total 1 second for fade on + fade off

    // Fade on
    for (int step = 0; step < steps; step++) {
        uint8_t brightness = (step * 255) / (steps - 1);
        for (uint16_t i = 0; i < numPixels; i++) {
            strip.setPixelColor(i, strip.Color(
                (brightness * 128) / 255,  // Red
                0,                         // Green
                (brightness * 128) / 255   // Blue
            ));
        }
        strip.show();
        delay(delayMs);
    }

    // Fade off
    for (int step = steps - 1; step >= 0; step--) {
        uint8_t brightness = (step * 255) / (steps - 1);
        for (uint16_t i = 0; i < numPixels; i++) {
            strip.setPixelColor(i, strip.Color(
                (brightness * 128) / 255,  // Red
                0,                         // Green
                (brightness * 128) / 255   // Blue
            ));
        }
        strip.show();
        delay(delayMs);
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

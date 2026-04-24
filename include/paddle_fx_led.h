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

// Draw a ball-hit flash using the provided RGB color.
inline void fxNeoBallHit(Adafruit_NeoPixel &strip, uint8_t r = 64, uint8_t g = 64, uint8_t b = 200) {
    const uint16_t n = strip.numPixels();
    if (n == 0) return;

    // Short "impact burst" using ONLY the configured swing-hit color.
    // Keep this effect brief: it runs on the swing FX worker task.
    const uint32_t base = strip.Color(r, g, b);

    const int centerL = (int)(n - 1) / 2;
    const int centerR = (int)n / 2;

    auto setAllDim = [&](uint8_t dim) {
        for (uint16_t i = 0; i < n; ++i) {
            strip.setPixelColor(i, strip.Color((r * dim) / 255, (g * dim) / 255, (b * dim) / 255));
        }
    };

    auto setPixDim = [&](int idx, uint8_t dim) {
        if (idx < 0 || idx >= (int)n) return;
        strip.setPixelColor((uint16_t)idx, strip.Color((r * dim) / 255, (g * dim) / 255, (b * dim) / 255));
    };

    // Frame 1: center "pop" (full color)
    strip.clear();
    strip.setPixelColor(centerL, base);
    strip.setPixelColor(centerR, base);
    strip.show();
    delay(18);

    // Frames 2-5: expanding pulse, with a dim glow behind it.
    for (int radius = 1; radius <= 4; ++radius) {
        strip.clear();
        setAllDim(22);

        // Core remains brighter early on.
        const uint8_t coreDim = (radius <= 2) ? 120 : 70;
        setPixDim(centerL, coreDim);
        setPixDim(centerR, coreDim);

        // Pulse heads at current radius (full), with a trailing shell (medium).
        setPixDim(centerL - radius, 255);
        setPixDim(centerR + radius, 255);
        setPixDim(centerL - radius + 1, 110);
        setPixDim(centerR + radius - 1, 110);
        strip.show();
        delay(14);
    }

    // Fade out quickly
    for (uint8_t dim : {18, 10, 0}) {
        strip.clear();
        if (dim) setAllDim(dim);
        strip.show();
        delay(12);
    }
}

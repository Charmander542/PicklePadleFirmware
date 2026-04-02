#include "NeoPixelStrip.h"
#include "paddle_fx_led.h"

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

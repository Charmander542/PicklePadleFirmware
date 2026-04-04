// paddle_fx.h — haptic tables (edit here). Neo: paddle_fx_led.h, speaker: paddle_fx_audio.h.

#pragma once

#include "paddle_fx_led.h"
#include "paddle_fx_audio.h"
#include "pins.h"
#include <HapticMux.h>

#define PADDLE_FX_STEP_COUNT(arr) (sizeof(arr) / sizeof((arr)[0]))

struct HapticStep {
    uint8_t channel;
    uint8_t effect;
    uint16_t holdMs;
};

static const HapticStep kFxBootHaptic[] = {
    {HapticMux::kAllHapticChannels, 47, 280}, {HapticMux::kAllHapticChannels, 1, 200},
    {HapticMux::kAllHapticChannels, 14, 320},
};
static const HapticStep kFxBallHitHaptic[] = {
    {HapticMux::kAllHapticChannels, 47, 150}, {HapticMux::kAllHapticChannels, 14, 200},
};
static const HapticStep kFxMenuTick[] = {
    {HapticMux::kAllHapticChannels, 1, 120},
};

inline void paddleFx_playSteps(HapticMux &mux, const HapticStep *steps, size_t count) {
    for (size_t i = 0; i < count; i++)
        mux.vibrate(steps[i].channel, steps[i].effect, steps[i].holdMs);
}

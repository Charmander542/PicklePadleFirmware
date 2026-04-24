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
    {HapticMux::kAllHapticChannels, 47, 150}, 
};

// Gameplay "swing" haptic: 3-motor launch-like buzz sequence.
// Assumes the three haptic motors are on mux channels 0,1,2.
static const HapticStep kFxSwingHaptic[] = {
    // Spin-up: staggered light buzz across motors.
    {0, 1, 55}, {1, 1, 55}, {2, 1, 55},
    {0, 1, 45}, {1, 1, 45}, {2, 1, 45},
    // Build: stronger buzz wave.
    {0, 14, 70}, {1, 14, 70}, {2, 14, 70},
    // Launch: short punch on each motor.
    {0, 47, 60}, {1, 47, 60}, {2, 47, 60},
};
static const HapticStep kFxMenuTick[] = {
    {HapticMux::kAllHapticChannels, 1, 120},
};

inline void paddleFx_playSteps(HapticMux &mux, const HapticStep *steps, size_t count) {
    for (size_t i = 0; i < count; i++)
        mux.vibrate(steps[i].channel, steps[i].effect, steps[i].holdMs);
}

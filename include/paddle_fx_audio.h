// Speaker / tone sequences (LEDC on SPEAKER_PWM). Tune frequencies and durations here.

#pragma once

#include <Arduino.h>
#include <stddef.h>
#include <SpeakerDriver.h>

struct SpeakerToneStep {
    uint16_t freqHz;
    uint16_t durMs;
};

static const SpeakerToneStep kFxBootSpeaker[] = {
    {523, 90},
    {659, 90},
    {784, 120},
    {1046, 150},
};

static const SpeakerToneStep kFxBallHitSpeaker[] = {
    {880, 35},
    {1320, 45},
    {660, 60},
};

#define PADDLE_FX_SPEAKER_COUNT(arr) (sizeof(arr) / sizeof((arr)[0]))

inline void paddleFx_playSpeakerSteps(SpeakerDriver &spk, const SpeakerToneStep *steps, size_t count) {
    for (size_t i = 0; i < count; i++) {
        spk.toneHz(steps[i].freqHz, steps[i].durMs);
    }
    spk.quiet();
}

// Speaker / tone sequences (LEDC on SPEAKER_PWM). Tune frequencies and durations here.

#pragma once

#include <Arduino.h>
#include <stddef.h>
#include <SpeakerDriver.h>

// ===== FULL NOTE TABLE =====

// Octave 4
#define NOTE_C4  262
#define NOTE_CS4 277  // C#
#define NOTE_D4  294
#define NOTE_DS4 311  // D# / Eb
#define NOTE_E4  330
#define NOTE_F4  349
#define NOTE_FS4 370  // F#
#define NOTE_G4  392
#define NOTE_GS4 415  // G#
#define NOTE_A4  440
#define NOTE_AS4 466  // A# / Bb
#define NOTE_B4  494

// Octave 5
#define NOTE_C5  523
#define NOTE_CS5 554
#define NOTE_D5  587
#define NOTE_DS5 622  // Eb5
#define NOTE_E5  659
#define NOTE_F5  698
#define NOTE_FS5 740
#define NOTE_G5  784
#define NOTE_GS5 831
#define NOTE_A5  880
#define NOTE_AS5 932  // Bb5
#define NOTE_B5  988

// Octave 6
#define NOTE_C6  1047
#define NOTE_CS6 1109
#define NOTE_D6  1175
#define NOTE_DS6 1245  // Eb6
#define NOTE_E6  1319
#define NOTE_F6  1397
#define NOTE_FS6 1480
#define NOTE_G6  1568
#define NOTE_GS6 1661
#define NOTE_A6  1760
#define NOTE_AS6 1865
#define NOTE_B6  1976

struct SpeakerToneStep {
    uint16_t freqHz;
    uint16_t durMs;
};

static const SpeakerToneStep kFxBootSpeaker[] = {
    {NOTE_C5, 90},
    {NOTE_E5, 90},
    {NOTE_G5, 120},
    {NOTE_C6, 150},
};

static const SpeakerToneStep kFxBallHitSpeaker[] = {
    {NOTE_A5, 35},
    {NOTE_E6, 45},
    {NOTE_E5, 60},
};

#define PADDLE_FX_SPEAKER_COUNT(arr) (sizeof(arr) / sizeof((arr)[0]))

inline void paddleFx_playSpeakerSteps(SpeakerDriver &spk, const SpeakerToneStep *steps, size_t count) {
    for (size_t i = 0; i < count; i++) {
        spk.toneHz(steps[i].freqHz, steps[i].durMs);
    }
    spk.quiet();
}

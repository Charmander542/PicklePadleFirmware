#include "SpeakerDriver.h"

bool SpeakerDriver::probe(gpio_num_t pin, uint8_t ledcChannel, uint8_t resolutionBits) {
    pin_ = pin;
    ch_ = ledcChannel;
    ok_ = (pin != GPIO_NUM_NC);
    if (!ok_) return false;
    ledcSetup(ch_, 2000, resolutionBits);
    ledcAttachPin(pin_, ch_);
    ledcWriteTone(ch_, 0);
    // Short inaudible tick to verify timer path.
    ledcWriteTone(ch_, 100);
    delay(5);
    ledcWriteTone(ch_, 0);
    return true;
}

void SpeakerDriver::toneHz(unsigned freq, uint32_t durMs) {
    if (!ok_) return;
    if (freq == 0) {
        quiet();
        delay(durMs);
        return;
    }
    ledcWriteTone(ch_, freq);
    delay(durMs);
    ledcWriteTone(ch_, 0);
}

void SpeakerDriver::quiet() {
    if (!ok_) return;
    ledcWriteTone(ch_, 0);
}

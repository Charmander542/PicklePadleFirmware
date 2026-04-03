#pragma once

#include <Arduino.h>
#include <driver/gpio.h>

class SpeakerDriver {
public:
    bool probe(gpio_num_t pin, uint8_t ledcChannel, uint8_t resolutionBits = 10);

    void toneHz(unsigned freq, uint32_t durMs);
    void quiet();

private:
    gpio_num_t pin_{GPIO_NUM_NC};
    uint8_t ch_{0};
    bool ok_{false};
};

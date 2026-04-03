#pragma once

#include <Arduino.h>
#include "pins.h"
#include "HapticMux.h"

class DisplayManager {
public:
    explicit DisplayManager(HapticMux &mux) : mux_(mux) {}

    // Assumes Wire.begin(...) already called on BUS pins.
    bool begin();

    void clear();
    void setLine(uint8_t line, const char *text);
    void showTwoLines(const char *title, const char *subtitle);
    void refresh();

private:
    void runInitSelfTest_();

    HapticMux &mux_;
    bool ok_{false};
    uint8_t *canvas_{nullptr};
};

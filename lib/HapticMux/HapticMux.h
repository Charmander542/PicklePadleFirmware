#pragma once

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_DRV2605.h>

class HapticMux {
public:
    void beginWire(int sdaPin, int sclPin, uint32_t clockHz = 100000);

    bool probeMux(uint8_t tcaAddr);

    // Scans all 8 channels for DRV2605 (0x5A / 0x5B). Returns number found.
    int scanDrivers();

    bool channelOk(uint8_t ch) const { return ch < 8 && hasDrv_[ch]; }

    // TCA9548A: write 0 disconnects all downstream ports — use before talking to
    // devices on the main bus (OLED, IMU) that are not behind the mux.
    void disableMuxBranches();

    void selectChannel(uint8_t channel);

    // Play effect (library waveform index) on one channel; blocks ~msDelay.
    void vibrate(uint8_t channel, uint8_t effect, uint16_t msDelay = 350);

    void bootSequenceVibrate();

    // Short strong pulse on first available DRV channel (host “swing hit”).
    void playHitFeedback();

private:
    bool i2cPresent_(uint8_t addr);
    uint8_t detectDrvAddr_();
    bool initDrvOnSelectedChannel_();
    bool writeTca_(uint8_t tcaAddr, uint8_t channel);

    Adafruit_DRV2605 drv_{};
    uint8_t tcaAddr_{0x70};
    bool hasMux_{false};
    bool hasDrv_[8]{};
    uint8_t drvAddr_[8]{};
};

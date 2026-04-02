#include "HapticMux.h"

void HapticMux::beginWire(int sdaPin, int sclPin, uint32_t clockHz) {
    Wire.begin(sdaPin, sclPin);
    Wire.setClock(clockHz);
}

bool HapticMux::i2cPresent_(uint8_t addr) {
    Wire.beginTransmission(addr);
    return Wire.endTransmission() == 0;
}

bool HapticMux::writeTca_(uint8_t tcaAddr, uint8_t channel) {
    if (channel > 7) return false;
    Wire.beginTransmission(tcaAddr);
    Wire.write(1u << channel);
    return Wire.endTransmission() == 0;
}

void HapticMux::selectChannel(uint8_t channel) {
    if (!hasMux_) return;
    writeTca_(tcaAddr_, channel);
    delay(2);
}

bool HapticMux::probeMux(uint8_t tcaAddr) {
    tcaAddr_ = tcaAddr;
    hasMux_ = i2cPresent_(tcaAddr);
    return hasMux_;
}

uint8_t HapticMux::detectDrvAddr_() {
    if (i2cPresent_(0x5A)) return 0x5A;
    if (i2cPresent_(0x5B)) return 0x5B;
    return 0;
}

bool HapticMux::initDrvOnSelectedChannel_() {
    if (!drv_.begin(&Wire)) return false;
    drv_.selectLibrary(1);
    drv_.setMode(DRV2605_MODE_INTTRIG);
    return true;
}

int HapticMux::scanDrivers() {
    for (int i = 0; i < 8; i++) {
        hasDrv_[i] = false;
        drvAddr_[i] = 0;
    }
    if (!hasMux_) return 0;

    int found = 0;
    for (uint8_t ch = 0; ch < 8; ch++) {
        selectChannel(ch);
        uint8_t addr = detectDrvAddr_();
        if (!addr) continue;
        drvAddr_[ch] = addr;
        if (!initDrvOnSelectedChannel_()) continue;
        hasDrv_[ch] = true;
        found++;
    }
    if (found > 0) selectChannel(0);
    return found;
}

void HapticMux::vibrate(uint8_t channel, uint8_t effect, uint16_t msDelay) {
    if (!hasMux_) return;
    if (channel > 7 || !hasDrv_[channel]) return;

    selectChannel(channel);
    if (!initDrvOnSelectedChannel_()) return;

    drv_.setWaveform(0, effect);
    drv_.setWaveform(1, 0);
    drv_.go();
    delay(msDelay);
}

void HapticMux::playHitFeedback() {
    if (!hasMux_) return;
    for (uint8_t ch = 0; ch < 8; ch++) {
        if (!hasDrv_[ch]) continue;
        vibrate(ch, 47, 180);
        break;
    }
}

void HapticMux::bootSequenceVibrate() {
    if (!hasMux_) return;
    const uint8_t pattern[] = {47, 1, 14};
    for (uint8_t ch = 0; ch < 8; ch++) {
        if (!hasDrv_[ch]) continue;
        for (uint8_t p : pattern) vibrate(ch, p, 260);
        delay(120);
    }
    selectChannel(0);
}

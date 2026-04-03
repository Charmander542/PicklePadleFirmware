#include "HapticMux.h"
#include "i2c_bus_lock.h"

void HapticMux::beginWire(int sdaPin, int sclPin, uint32_t clockHz) {
    I2cBusLock lk;
    Wire.begin(sdaPin, sclPin);
    Wire.setClock(clockHz);
    Wire.setTimeOut(200);  // ms; default can be tight with WiFi / preemption
}

bool HapticMux::i2cPresent_(uint8_t addr) {
    I2cBusLock lk;
    Wire.beginTransmission(addr);
    return Wire.endTransmission() == 0;
}

bool HapticMux::writeTca_(uint8_t tcaAddr, uint8_t channel) {
    if (channel > 7) return false;
    I2cBusLock lk;
    Wire.beginTransmission(tcaAddr);
    Wire.write(1u << channel);
    return Wire.endTransmission() == 0;
}

void HapticMux::disableMuxBranches() {
    I2cBusLock lk;
    Wire.beginTransmission(tcaAddr_);
    Wire.write(0);
    (void)Wire.endTransmission();
    delay(1);
}

void HapticMux::selectChannel(uint8_t channel) {
    if (channel > 7) return;
    // Always drive the TCA9548A select: probe can false-negative while the chip is
    // still on the bus. If there is no mux, this NACKs harmlessly.
    I2cBusLock lk;
    (void)writeTca_(tcaAddr_, channel);
    delay(2);
}

bool HapticMux::probeMux(uint8_t tcaAddr) {
    I2cBusLock lk;
    tcaAddr_ = tcaAddr;
    for (int attempt = 0; attempt < 3; attempt++) {
        if (i2cPresent_(tcaAddr)) {
            hasMux_ = true;
            return true;
        }
        delay(5);
    }
    hasMux_ = false;
    return false;
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
    I2cBusLock lk;
    for (int i = 0; i < 8; i++) {
        hasDrv_[i] = false;
        drvAddr_[i] = 0;
    }

    if (!hasMux_) {
        selectChannel(0);
        uint8_t addr = detectDrvAddr_();
        if (!addr) {
            disableMuxBranches();
            return 0;
        }
        drvAddr_[0] = addr;
        if (!initDrvOnSelectedChannel_()) {
            disableMuxBranches();
            return 0;
        }
        hasDrv_[0] = true;
        disableMuxBranches();
        return 1;
    }

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
    disableMuxBranches();
    return found;
}

void HapticMux::vibrate(uint8_t channel, uint8_t effect, uint16_t msDelay) {
    if (channel > 7 || !hasDrv_[channel]) return;

    I2cBusLock lk;
    selectChannel(channel);
    if (!initDrvOnSelectedChannel_()) {
        disableMuxBranches();
        return;
    }

    drv_.setWaveform(0, effect);
    drv_.setWaveform(1, 0);
    drv_.go();
    delay(msDelay);
    disableMuxBranches();
}

void HapticMux::playHitFeedback() {
    for (uint8_t ch = 0; ch < 8; ch++) {
        if (!hasDrv_[ch]) continue;
        vibrate(ch, 47, 180);
        break;
    }
}

void HapticMux::bootSequenceVibrate() {
    const uint8_t pattern[] = {47, 1, 14};
    for (uint8_t ch = 0; ch < 8; ch++) {
        if (!hasDrv_[ch]) continue;
        for (uint8_t p : pattern) vibrate(ch, p, 260);
        delay(120);
    }
    disableMuxBranches();
}

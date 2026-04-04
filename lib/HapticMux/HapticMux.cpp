#include "HapticMux.h"
#include "i2c_bus_lock.h"

void HapticMux::beginWire(int sdaPin, int sclPin, uint32_t clockHz) {
    sdaPin_ = sdaPin;
    sclPin_ = sclPin;
    I2cBusLock lk;
    Wire.begin(sdaPin, sclPin);
    Wire.setClock(clockHz);
    Wire.setTimeOut(200);
}

void HapticMux::wireRestore_() {
    Wire.setClock(100000);
    Wire.setTimeOut(200);
}

bool HapticMux::i2cPresent_(uint8_t addr) {
    Wire.beginTransmission(addr);
    return Wire.endTransmission() == 0;
}

void HapticMux::muxSelectCh_(uint8_t channel) {
    if (channel > 7) return;
    Wire.beginTransmission(tcaAddr_);
    Wire.write(1u << channel);
    (void)Wire.endTransmission();
    delay(2);
}

void HapticMux::muxDisable_() {
    Wire.beginTransmission(tcaAddr_);
    Wire.write(0);
    (void)Wire.endTransmission();
    delay(1);
}

void HapticMux::disableMuxBranches() {
    I2cBusLock lk;
    muxDisable_();
}

void HapticMux::selectChannel(uint8_t channel) {
    if (channel > 7) return;
    I2cBusLock lk;
    muxSelectCh_(channel);
}

bool HapticMux::probeMux(uint8_t tcaAddr) {
    I2cBusLock lk;
    tcaAddr_ = tcaAddr;
    wireRestore_();
    for (int attempt = 0; attempt < 3; attempt++) {
        if (i2cPresent_(tcaAddr)) {
            hasMux_ = true;
            Serial.printf("[haptic] TCA9548A found at 0x%02X\r\n", tcaAddr);
            return true;
        }
        delay(5);
    }
    hasMux_ = false;
    Serial.printf("[haptic] TCA9548A NOT found at 0x%02X (will still scan all channels)\r\n", tcaAddr);
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
    drvReady_ = true;
    return true;
}

int HapticMux::scanDrivers() {
    I2cBusLock lk;
    wireRestore_();

    for (int i = 0; i < 8; i++) {
        hasDrv_[i] = false;
        drvAddr_[i] = 0;
    }
    drvReady_ = false;

    // Phase 1: DRV on main bus (mux all ports off).
    muxDisable_();
    delay(5);
    uint8_t addr = detectDrvAddr_();
    if (addr) {
        Serial.printf("[haptic] DRV on main bus at 0x%02X\r\n", addr);
        drvAddr_[0] = addr;
        if (initDrvOnSelectedChannel_()) {
            hasDrv_[0] = true;
            Serial.println("[haptic] DRV init OK on main bus → ch0");
            muxDisable_();
            return 1;
        }
        Serial.println("[haptic] DRV init FAILED on main bus");
        drvAddr_[0] = 0;
    }

    // Phase 2: DRVs behind TCA9548A (scan every branch).
    int found = 0;
    for (uint8_t ch = 0; ch < 8; ch++) {
        muxSelectCh_(ch);
        addr = detectDrvAddr_();
        if (!addr) continue;
        Serial.printf("[haptic] mux ch%u → DRV at 0x%02X", ch, addr);
        drvAddr_[ch] = addr;
        if (!initDrvOnSelectedChannel_()) {
            Serial.println(" init FAILED");
            drvAddr_[ch] = 0;
            continue;
        }
        hasDrv_[ch] = true;
        found++;
        Serial.println(" init OK");
    }
    muxDisable_();
    Serial.printf("[haptic] scan complete: %d DRV(s) found\r\n", found);
    return found;
}

void HapticMux::vibrate(uint8_t channel, uint8_t effect, uint16_t msDelay) {
    auto playOne = [&](uint8_t ch) -> bool {
        wireRestore_();
        muxSelectCh_(ch);

        // Only full-init once; after that just select mux and send commands.
        // The DRV keeps its register state across mux switches.
        if (!drvReady_) {
            if (!initDrvOnSelectedChannel_()) {
                Serial.printf("[haptic] vibrate ch%u: DRV init failed\r\n", ch);
                muxDisable_();
                return false;
            }
        }

        drv_.setWaveform(0, effect);
        drv_.setWaveform(1, 0);
        drv_.go();
        delay(msDelay);
        muxDisable_();
        return true;
    };

    if (channel == kAllHapticChannels) {
        I2cBusLock lk;
        for (uint8_t ch = 0; ch < 8; ch++) {
            if (!hasDrv_[ch]) continue;
            playOne(ch);
        }
        return;
    }
    if (channel > 7 || !hasDrv_[channel]) return;

    I2cBusLock lk;
    playOne(channel);
}

void HapticMux::playHitFeedback() {
    vibrate(kAllHapticChannels, 47, 180);
}

void HapticMux::bootSequenceVibrate() {
    const uint8_t pattern[] = {47, 1, 14};
    I2cBusLock lk;
    wireRestore_();
    for (uint8_t ch = 0; ch < 8; ch++) {
        if (!hasDrv_[ch]) continue;
        Serial.printf("[haptic] boot test ch%u (0x%02X)\r\n", ch, drvAddr_[ch]);
        for (uint8_t p : pattern) {
            muxSelectCh_(ch);
            if (!drvReady_) {
                if (!initDrvOnSelectedChannel_()) {
                    Serial.printf("[haptic] boot ch%u init failed\r\n", ch);
                    muxDisable_();
                    continue;
                }
            }
            drv_.setWaveform(0, p);
            drv_.setWaveform(1, 0);
            drv_.go();
            delay(260);
            muxDisable_();
        }
        delay(120);
    }
    muxDisable_();
}

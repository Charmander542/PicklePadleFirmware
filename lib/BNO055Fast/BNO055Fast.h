#pragma once

#include <Arduino.h>
#include <Wire.h>
#include <Preferences.h>

struct Quaternion {
    float w, x, y, z;
};

struct Vec3 {
    float x, y, z;
};

class BNO055Fast {
public:
    explicit BNO055Fast(TwoWire *wire = &Wire, uint8_t addr = 0x28);

    // Wire.begin(sda, scl) must already have been called on `wire`.
    bool begin(uint32_t twiClockHz = 400000);

    bool update();
    Quaternion getQuat();

    bool readLinearAccel();
    Vec3 getLinearAccel() const { return _linearAccel; }

    void getCalibration(uint8_t &sys, uint8_t &gyro, uint8_t &accel, uint8_t &mag);
    bool isFullyCalibrated();
    void saveOffsets();
    bool loadOffsets();

private:
    TwoWire *_wire;
    uint8_t _addr;

    Quaternion quat;
    Vec3 _linearAccel{};
    Preferences prefs;

    bool write8(uint8_t reg, uint8_t value);
    bool readLen(uint8_t reg, uint8_t *data, size_t len);
    bool readQuaternion();
};

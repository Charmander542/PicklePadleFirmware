#include "BNO055Fast.h"

#define BNO055_QUATERNION_DATA_W_LSB 0x20
#define BNO055_LINEAR_ACCEL_DATA_X_LSB 0x28
#define BNO055_OPR_MODE 0x3D
#define BNO055_PWR_MODE 0x3E
#define BNO055_CALIB_STAT 0x35
#define BNO055_OFFSET_START 0x55
#define BNO055_OFFSET_LENGTH 22

BNO055Fast::BNO055Fast(TwoWire *wire, uint8_t addr) : _wire(wire), _addr(addr) {}

bool BNO055Fast::begin(uint32_t twiClockHz) {
    if (!_wire) return false;
    _wire->setClock(twiClockHz);

    write8(BNO055_OPR_MODE, 0x00);
    delay(10);

    write8(BNO055_PWR_MODE, 0x00);
    delay(10);

    write8(BNO055_OPR_MODE, 0x0C);
    delay(20);

    loadOffsets();
    return true;
}

bool BNO055Fast::update() { return readQuaternion(); }

Quaternion BNO055Fast::getQuat() { return quat; }

bool BNO055Fast::readLinearAccel() {
    uint8_t buffer[6];
    if (!readLen(BNO055_LINEAR_ACCEL_DATA_X_LSB, buffer, 6)) return false;

    int16_t rawX = (buffer[1] << 8) | buffer[0];
    int16_t rawY = (buffer[3] << 8) | buffer[2];
    int16_t rawZ = (buffer[5] << 8) | buffer[4];

    const float scale = 1.0f / 100.0f;
    _linearAccel.x = rawX * scale;
    _linearAccel.y = rawY * scale;
    _linearAccel.z = rawZ * scale;
    return true;
}

void BNO055Fast::getCalibration(uint8_t &sys, uint8_t &gyro, uint8_t &accel, uint8_t &mag) {
    uint8_t cal = 0;
    readLen(BNO055_CALIB_STAT, &cal, 1);
    sys = (cal >> 6) & 0x03;
    gyro = (cal >> 4) & 0x03;
    accel = (cal >> 2) & 0x03;
    mag = (cal >> 0) & 0x03;
}

bool BNO055Fast::isFullyCalibrated() {
    uint8_t sys, gyro, accel, mag;
    getCalibration(sys, gyro, accel, mag);
    return sys == 3 && gyro == 3 && accel == 3 && mag == 3;
}

void BNO055Fast::saveOffsets() {
    uint8_t offsets[BNO055_OFFSET_LENGTH];
    if (!readLen(BNO055_OFFSET_START, offsets, BNO055_OFFSET_LENGTH)) return;

    prefs.begin("BNO055", false);
    prefs.putBytes("offsets", offsets, BNO055_OFFSET_LENGTH);
    prefs.end();
}

bool BNO055Fast::loadOffsets() {
    uint8_t offsets[BNO055_OFFSET_LENGTH];
    prefs.begin("BNO055", true);
    size_t len = prefs.getBytes("offsets", offsets, BNO055_OFFSET_LENGTH);
    prefs.end();

    if (len != BNO055_OFFSET_LENGTH) return false;

    for (int i = 0; i < BNO055_OFFSET_LENGTH; i++) {
        write8(BNO055_OFFSET_START + i, offsets[i]);
    }
    return true;
}

bool BNO055Fast::readQuaternion() {
    uint8_t buffer[8];
    if (!readLen(BNO055_QUATERNION_DATA_W_LSB, buffer, 8)) return false;

    int16_t w = (buffer[1] << 8) | buffer[0];
    int16_t x = (buffer[3] << 8) | buffer[2];
    int16_t y = (buffer[5] << 8) | buffer[4];
    int16_t z = (buffer[7] << 8) | buffer[6];

    const float scale = 1.0f / (1 << 14);
    quat.w = w * scale;
    quat.x = x * scale;
    quat.y = y * scale;
    quat.z = z * scale;

    return true;
}

bool BNO055Fast::write8(uint8_t reg, uint8_t value) {
    _wire->beginTransmission(_addr);
    _wire->write(reg);
    _wire->write(value);
    return _wire->endTransmission(true) == 0;
}

bool BNO055Fast::readLen(uint8_t reg, uint8_t *data, size_t len) {
    if (len > 255) return false;
    _wire->beginTransmission(_addr);
    _wire->write(reg);
    if (_wire->endTransmission(false) != 0) return false;
    const uint8_t n = _wire->requestFrom(_addr, (uint8_t)len);
    if (n != len) return false;
    for (size_t i = 0; i < len; i++)
        data[i] = (uint8_t)_wire->read();
    return true;
}

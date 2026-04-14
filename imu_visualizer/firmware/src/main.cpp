#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BNO055.h>
#include <utility/imumaths.h>

#include "pins.h"

static constexpr uint32_t kStreamIntervalMs = 10; // ~100 Hz (viewer uses GPU VBO; host can keep up)
static Adafruit_BNO055 bno(55, BNO055_ADDR);
static bool imuReady = false;

void setup() {
    Serial.begin(115200);
    // Do NOT use `while (!Serial)` on classic ESP32 (UART USB bridge): Serial may
    // never become "ready", setup blocks forever, and the task WDT resets (TG1WDT_SYS_RESET).
#if ARDUINO_USB_CDC_ON_BOOT
    while (!Serial) {
        delay(10);
    }
#else
    delay(200);
#endif

    Wire.begin(IMU_SDA, IMU_SCL);
    Wire.setClock(400000);

    Serial.println("# BNO055 IMU Streamer");
    Serial.println("# Waiting for sensor...");

    for (int attempt = 0; attempt < 10; attempt++) {
        if (bno.begin()) {
            imuReady = true;
            break;
        }
        Serial.printf("# BNO055 not found (attempt %d/10)\n", attempt + 1);
        delay(500);
    }

    if (!imuReady) {
        Serial.println("# ERROR: BNO055 not detected. Check wiring / address.");
        return;
    }

    bno.setExtCrystalUse(true);
    delay(100);

    Serial.println("# Sensor ready. Streaming quaternion + euler.");
    Serial.println("# Format: Q,w,x,y,z,ex,ey,ez,cal_sys,cal_gyro,cal_accel,cal_mag");
}

void loop() {
    if (!imuReady) {
        delay(1000);
        return;
    }

    static uint32_t lastStream = 0;
    uint32_t now = millis();
    if (now - lastStream < kStreamIntervalMs) return;
    lastStream = now;

    imu::Quaternion q = bno.getQuat();
    imu::Vector<3> euler = bno.getVector(Adafruit_BNO055::VECTOR_EULER);

    uint8_t sys, gyro, accel, mag;
    bno.getCalibration(&sys, &gyro, &accel, &mag);

    Serial.printf("Q,%.6f,%.6f,%.6f,%.6f,%.2f,%.2f,%.2f,%u,%u,%u,%u\n",
                  q.w(), q.x(), q.y(), q.z(),
                  euler.x(), euler.y(), euler.z(),
                  sys, gyro, accel, mag);
}

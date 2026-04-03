// jerk_detect.h — header-only helper for impulse / “wing hit” style events from IMU linear accel.
//
// Feed linear acceleration (m/s^2). Uses |Δa|/Δt on lightly low-pass-filtered samples.

#pragma once

#include <Arduino.h>
#include <math.h>

struct Vec3 {
    float x, y, z;
};

class JerkDetector {
public:
    void configure(float jerkThreshold_mps3, uint32_t minRetriggerMs = 120,
                   float accelLpfAlpha = 0.2f) {
        threshold_ = jerkThreshold_mps3;
        minRetriggerMs_ = minRetriggerMs;
        lpfAlpha_ = constrain(accelLpfAlpha, 0.01f, 1.f);
        reset();
    }

    void reset() {
        hasPrev_ = false;
        lastTrigMs_ = 0;
        lastJerk_ = 0.f;
    }

    float lastJerkMagnitude() const { return lastJerk_; }

    bool update(const Vec3 &accel_mps2, uint32_t nowMs = 0) {
        if (nowMs == 0) nowMs = millis();

        if (!hasPrev_) {
            filt_ = accel_mps2;
            prevFilt_ = accel_mps2;
            prevMs_ = nowMs;
            hasPrev_ = true;
            lastJerk_ = 0.f;
            return false;
        }

        float dt = (nowMs - prevMs_) * 0.001f;
        prevMs_ = nowMs;
        if (dt < 0.0005f) return false;

        filt_.x = lpfAlpha_ * accel_mps2.x + (1.f - lpfAlpha_) * filt_.x;
        filt_.y = lpfAlpha_ * accel_mps2.y + (1.f - lpfAlpha_) * filt_.y;
        filt_.z = lpfAlpha_ * accel_mps2.z + (1.f - lpfAlpha_) * filt_.z;

        float dax = filt_.x - prevFilt_.x;
        float day = filt_.y - prevFilt_.y;
        float daz = filt_.z - prevFilt_.z;
        prevFilt_ = filt_;

        lastJerk_ = sqrtf(dax * dax + day * day + daz * daz) / dt;

        if (lastJerk_ >= threshold_ &&
            (lastTrigMs_ == 0 || (nowMs - lastTrigMs_) >= minRetriggerMs_)) {
            lastTrigMs_ = nowMs;
            return true;
        }
        return false;
    }

private:
    float threshold_ = 5000.f;
    uint32_t minRetriggerMs_ = 120;
    float lpfAlpha_ = 0.2f;

    Vec3 filt_{};
    Vec3 prevFilt_{};
    uint32_t prevMs_ = 0;
    bool hasPrev_ = false;
    uint32_t lastTrigMs_ = 0;
    float lastJerk_ = 0.f;
};

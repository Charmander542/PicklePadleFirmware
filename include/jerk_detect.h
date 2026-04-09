// jerk_detect.h — header-only helper for impulse / “wing hit” style events from IMU linear accel.
//
// Feed linear acceleration (m/s^2). Uses |Δa|/Δt on lightly low-pass-filtered samples.
// Pass nowUs = micros() (not millis()) so Δt is not stuck at 0 ms and 1/dt does not blow up.

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
        threshold_        = jerkThreshold_mps3;
        minRetriggerMs_   = minRetriggerMs;
        lpfAlpha_         = constrain(accelLpfAlpha, 0.01f, 1.f);
        reset();
    }

    void reset() {
        hasPrev_    = false;
        lastTrigMs_ = 0;
        lastJerk_   = 0.f;
    }

    float lastJerkMagnitude() const { return lastJerk_; }

    /**
     * @param nowUs  Monotonic time in microseconds (e.g. micros()). Do not use millis() here:
     *               1 ms resolution can make Δt==0 between samples → enormous bogus jerk.
     */
    bool update(const Vec3 &accel_mps2, uint32_t nowUs = 0) {
        if (nowUs == 0) {
            nowUs = micros();
        }

        if (!hasPrev_) {
            filt_     = accel_mps2;
            prevFilt_ = accel_mps2;
            prevUs_   = nowUs;
            hasPrev_  = true;
            lastJerk_ = 0.f;
            return false;
        }

        // Unsigned diff is correct across micros() wrap for intervals ≪ ~2^31 µs.
        const float dt = static_cast<float>(nowUs - prevUs_) * 1e-6f;
        if (dt < kMinDtSec_) {
            // Same or backward time step — do not advance state or you desync filter vs clock.
            return false;
        }

        prevUs_ = nowUs;

        filt_.x = lpfAlpha_ * accel_mps2.x + (1.f - lpfAlpha_) * filt_.x;
        filt_.y = lpfAlpha_ * accel_mps2.y + (1.f - lpfAlpha_) * filt_.y;
        filt_.z = lpfAlpha_ * accel_mps2.z + (1.f - lpfAlpha_) * filt_.z;

        const float dax = filt_.x - prevFilt_.x;
        const float day = filt_.y - prevFilt_.y;
        const float daz = filt_.z - prevFilt_.z;
        prevFilt_       = filt_;

        lastJerk_ = sqrtf(dax * dax + day * day + daz * daz) / dt;

        const uint32_t wallMs = millis();
        if (lastJerk_ >= threshold_ &&
            (lastTrigMs_ == 0 || (wallMs - lastTrigMs_) >= minRetriggerMs_)) {
            lastTrigMs_ = wallMs;
            return true;
        }
        return false;
    }

private:
    static constexpr float kMinDtSec_ = 200e-6f;  // ignore sub-200 µs spacing (noise / batching)

    float    threshold_      = 5000.f;
    uint32_t minRetriggerMs_ = 120;
    float    lpfAlpha_       = 0.2f;

    Vec3     filt_{};
    Vec3     prevFilt_{};
    uint32_t prevUs_   = 0;
    bool     hasPrev_  = false;
    uint32_t lastTrigMs_ = 0;
    float    lastJerk_   = 0.f;
};

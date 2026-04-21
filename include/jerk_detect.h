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
                   float accelLpfAlpha = 0.2f,
                   uint32_t jerkWindowUs = 2000) {
        threshold_        = jerkThreshold_mps3;
        minRetriggerMs_   = minRetriggerMs;
        lpfAlpha_         = constrain(accelLpfAlpha, 0.01f, 1.f);
        windowUs_          = jerkWindowUs;
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
            // Initialize history buffer to the first sample/time so early queries
            // don't read zeros as old samples.
            for (size_t i = 0; i < kHistorySize_; ++i) {
                history_[i] = prevFilt_;
                historyUs_[i] = prevUs_;
            }
            historyIdx_ = 0;
            return false;
        }

        // Unsigned diff is correct across micros() wrap for intervals ≪ ~2^31 µs.
        const float dt = static_cast<float>(nowUs - prevUs_) * 1e-6f;
        if (dt < kMinDtSec_) {
            // Same or backward time step — do not advance state or you desync filter vs clock.
            return false;
        }

        // Preserve previous timestamp for history insertion, then update current time.
        const uint32_t prevUsOld = prevUs_;
        prevUs_ = nowUs;

        // Update LPF with the new sample.
        filt_.x = lpfAlpha_ * accel_mps2.x + (1.f - lpfAlpha_) * filt_.x;
        filt_.y = lpfAlpha_ * accel_mps2.y + (1.f - lpfAlpha_) * filt_.y;
        filt_.z = lpfAlpha_ * accel_mps2.z + (1.f - lpfAlpha_) * filt_.z;

        // Push previous filtered sample into history for windowed delta calculation.
        // Use the previous timestamp (prevUsOld) so history entries reflect the
        // time the prevFilt_ sample was taken.
        history_[historyIdx_] = prevFilt_;
        historyUs_[historyIdx_] = prevUsOld;
        historyIdx_ = (historyIdx_ + 1) & (kHistoryMask_);

        // Find the oldest history entry at or before nowUs - windowUs_. Scan back up to buffer size.
        const uint32_t targetUs = nowUs - windowUs_;
        int found = -1;
        for (size_t i = 0; i < kHistorySize_; ++i) {
            size_t idx = (historyIdx_ + kHistorySize_ - 1 - i) & (kHistoryMask_);
            if (historyUs_[idx] <= targetUs) {
                found = (int)idx;
                break;
            }
        }

        Vec3 ref = prevFilt_; // fallback to single-step behavior
        uint32_t refUs = prevUs_;
        if (found >= 0) {
            ref = history_[found];
            refUs = historyUs_[found];
        }

        const float dtWindow = static_cast<float>(nowUs - refUs) * 1e-6f;
        if (dtWindow < kMinDtSec_) {
            // fallback: very small window, use one-step delta
            const float dax = filt_.x - prevFilt_.x;
            const float day = filt_.y - prevFilt_.y;
            const float daz = filt_.z - prevFilt_.z;
            lastJerk_ = sqrtf(dax * dax + day * day + daz * daz) / dt;
        } else {
            const float dax = filt_.x - ref.x;
            const float day = filt_.y - ref.y;
            const float daz = filt_.z - ref.z;
            lastJerk_ = sqrtf(dax * dax + day * day + daz * daz) / dtWindow;
        }

        prevFilt_ = filt_;

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
    // History buffer for windowed jerk calculation (power-of-two size).
    static constexpr size_t kHistorySize_ = 32;
    static constexpr size_t kHistoryMask_ = kHistorySize_ - 1;
    Vec3     history_[kHistorySize_]{};
    uint32_t historyUs_[kHistorySize_]{};
    size_t   historyIdx_ = 0;
    // Jerk window to compare against (microseconds).
    uint32_t windowUs_ = 2000;
};

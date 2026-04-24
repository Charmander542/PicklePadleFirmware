#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <Preferences.h>
#include <stdarg.h>
#include <stdio.h>
#include <esp_system.h>

#include "pins.h"
#include "i2c_bus_lock.h"
#include "app_config.h"
#include "app_state.h"
#include "jerk_detect.h"
#include "paddle_fx.h"

#include <Adafruit_Sensor.h>
#include <Adafruit_BNO055.h>
#include <utility/imumaths.h>

#include <NeoPixelStrip.h>
#include <SpeakerDriver.h>
#include <HapticMux.h>
#include <DisplayManager.h>
#include <WifiPortal.h>
#include <NetUdp.h>
#include <SdLogger.h>

static NeoPixelStrip gStrip;
static SpeakerDriver gSpk;
static HapticMux gMux;
static DisplayManager gDisp(gMux);
static Adafruit_BNO055 gBno(55, kBno055I2cAddr);
static JerkDetector gJerk;
static NetUdp gNet;
static WifiPortal gPortal;
static bool s_imuReady = false;
/** After STA connect, show IP until UE sends UDP `idle` (UiEvent::ModeIdle). */
static bool s_showPaddleIpUntilUeIdle = true;
static portMUX_TYPE s_swingAudioMux = portMUX_INITIALIZER_UNLOCKED;
static bool s_swingAudioBusy = false;

static void swingHitAudioTask(void * /*param*/) {
    paddleFx_playSpeakerSteps(gSpk, kFxBallHitSpeaker, PADDLE_FX_SPEAKER_COUNT(kFxBallHitSpeaker));
    portENTER_CRITICAL(&s_swingAudioMux);
    s_swingAudioBusy = false;
    portEXIT_CRITICAL(&s_swingAudioMux);
    vTaskDelete(nullptr);
}

/** Queue depth kSwingFxQueueDepth: non-blocking post from net/app; worker plays LED then haptics. */
static QueueHandle_t g_swingFxQueue = nullptr;

enum class SwingFxKind : uint8_t {
    HostSwingHit = 1,
    GameplaySwing = 2,
};

struct SwingFxMsg {
    SwingFxKind kind;
};

static void swingFxWorkerTask(void * /*param*/) {
    SwingFxMsg msg{};
    for (;;) {
        if (xQueueReceive(g_swingFxQueue, &msg, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        switch (msg.kind) {
        case SwingFxKind::HostSwingHit:
            // NeoPixel first (no I²C) so feedback starts immediately; then DRV path (longer, bus-heavy).
            gStrip.playBallHit();
            paddleFx_playSteps(gMux, kFxBallHitHaptic, PADDLE_FX_STEP_COUNT(kFxBallHitHaptic));
            break;
        case SwingFxKind::GameplaySwing:
            // Gameplay jerk trigger: haptics only (keep LED reserved for host swing-hit feedback).
            paddleFx_playSteps(gMux, kFxSwingHaptic, PADDLE_FX_STEP_COUNT(kFxSwingHaptic));
            break;
        default:
            break;
        }
    }
}

/** Enqueue swing-hit LED + haptic work; never blocks the caller (drops if queue full). */
static void queueSwingHostFx() {
    if (!g_swingFxQueue) {
        return;
    }
    SwingFxMsg msg{SwingFxKind::HostSwingHit};
    (void)xQueueSend(g_swingFxQueue, &msg, 0);
}

/** Enqueue gameplay "swing" haptics; never blocks the caller (drops if queue full). */
static void queueGameplaySwingFx() {
    if (!g_swingFxQueue) {
        return;
    }
    SwingFxMsg msg{SwingFxKind::GameplaySwing};
    (void)xQueueSend(g_swingFxQueue, &msg, 0);
}

static void triggerSwingHitAudioAsync() {
    bool canStart = false;
    portENTER_CRITICAL(&s_swingAudioMux);
    if (!s_swingAudioBusy) {
        s_swingAudioBusy = true;
        canStart = true;
    }
    portEXIT_CRITICAL(&s_swingAudioMux);

    if (!canStart) {
        return;
    }

    constexpr uint32_t kSwingAudioStack = 2048;
    constexpr UBaseType_t kSwingAudioPriority = 2;
    if (xTaskCreatePinnedToCore(swingHitAudioTask, "fx_audio", kSwingAudioStack, nullptr,
                                kSwingAudioPriority, nullptr, 0) != pdPASS) {
        // Fallback: if task creation fails, play synchronously to avoid dropping feedback.
        paddleFx_playSpeakerSteps(gSpk, kFxBallHitSpeaker, PADDLE_FX_SPEAKER_COUNT(kFxBallHitSpeaker));
        portENTER_CRITICAL(&s_swingAudioMux);
        s_swingAudioBusy = false;
        portEXIT_CRITICAL(&s_swingAudioMux);
    }
}

static wifi_power_t mapDbmToWifiPower(int8_t dbm) {
    if (dbm <= 2) return WIFI_POWER_2dBm;
    if (dbm <= 5) return WIFI_POWER_5dBm;
    if (dbm <= 7) return WIFI_POWER_7dBm;
    if (dbm <= 8) return WIFI_POWER_8_5dBm;
    if (dbm <= 11) return WIFI_POWER_11dBm;
    if (dbm <= 13) return WIFI_POWER_13dBm;
    if (dbm <= 15) return WIFI_POWER_15dBm;
    if (dbm <= 17) return WIFI_POWER_17dBm;
    if (dbm <= 18) return WIFI_POWER_18_5dBm;
    return WIFI_POWER_19_5dBm;
}

/**
 * Streaming / gameplay: higher TX + no modem sleep. Intentionally does NOT call
 * esp_wifi_set_protocol / esp_wifi_set_bandwidth while associated — those often force
 * a link teardown (wifi: state run -> init) on home APs right after the first mode command.
 */
static void applyWifiStreamingBoost() {
    if (WiFi.status() != WL_CONNECTED) return;

    WiFi.setSleep(false);
    WiFi.setTxPower(mapDbmToWifiPower(kWifiStreamingTxPowerDbm));
    (void)esp_wifi_set_ps(WIFI_PS_NONE);
}

/** Idle: lower TX than streaming; keep PS_NONE so UDP command wake stays responsive. */
static void applyWifiIdleRadio() {
    if (WiFi.status() != WL_CONNECTED) return;

    WiFi.setSleep(false);
    WiFi.setTxPower(mapDbmToWifiPower(kWifiRunTxPowerDbm));
    (void)esp_wifi_set_ps(WIFI_PS_NONE);
}

static bool imuBeginAdafruit() {
    I2cBusLock lk;
    gMux.disableMuxBranches();
    // Adafruit begin() polls until the chip ACKs; slow clock + settle reduces failed tries
    // (each failure logs Wire "[E] requestFrom..."). Bus pins already set in beginWire().
    Wire.setTimeOut(400);
    Wire.setClock(50000);
    delay(120);
    if (!gBno.begin()) {
        return false;
    }
    delay(100);
    gBno.setExtCrystalUse(true);
    Wire.setClock(kImuI2cHz);
    Wire.setTimeOut(kImuWireTimeoutMs);  // 263 on Wire = ESP_ERR_TIMEOUT; avoid under WiFi
    return true;
}

static void imuReadLinear(Vec3 *out) {
    I2cBusLock lk;
    gMux.disableMuxBranches();
    Wire.setTimeOut(kImuWireTimeoutMs);
    Wire.setClock(kImuReadClockHz);
    imu::Vector<3> ev = gBno.getVector(Adafruit_BNO055::VECTOR_LINEARACCEL);
    out->x = (float)ev.x();
    out->y = (float)ev.y();
    out->z = (float)ev.z();
    Wire.setClock(kImuI2cHz);
}

/** One bus lock: linear accel (jerk) + Euler degrees — same CSV row shape as tutorial flood. */
static void imuReadLinearEuler(Vec3 *linearOut, Vec3 *eulerOut) {
    I2cBusLock lk;
    gMux.disableMuxBranches();
    Wire.setTimeOut(kImuWireTimeoutMs);
    Wire.setClock(kImuReadClockHz);
    imu::Vector<3> la = gBno.getVector(Adafruit_BNO055::VECTOR_LINEARACCEL);
    linearOut->x = (float)la.x();
    linearOut->y = (float)la.y();
    linearOut->z = (float)la.z();
    imu::Vector<3> ev = gBno.getVector(Adafruit_BNO055::VECTOR_EULER);
    eulerOut->x = (float)ev.x();
    eulerOut->y = (float)ev.y();
    eulerOut->z = (float)ev.z();
    Wire.setClock(kImuI2cHz);
}

/**
 * Blast read for tutorial flood: quat + euler + cal + linear accel in ONE lock scope at 400 kHz.
 * No mux disable per call (done once at mode entry), no clock restore.
 */
struct ImuSnapshot {
    float qw, qx, qy, qz;
    float ex, ey, ez;
    float ax, ay, az;
    uint8_t calSys, calGyro, calAccel, calMag;
};

/** After tutorial flood (400 kHz / short timeout), restore bus for OLED/mux + gameplay IMU reads. */
static void restoreImuWireAfterTutorialBurst() {
    I2cBusLock lk;
    gMux.disableMuxBranches();
    Wire.setClock(kImuI2cHz);
    Wire.setTimeOut(kImuWireTimeoutMs);
}

static void imuReadSnapshotFast(ImuSnapshot *s) {
    I2cBusLock lk;
    Wire.setClock(kImuFastClockHz);
    Wire.setTimeOut(kImuFastWireTimeoutMs);
    imu::Quaternion q = gBno.getQuat();
    s->qw = (float)q.w(); s->qx = (float)q.x();
    s->qy = (float)q.y(); s->qz = (float)q.z();
    imu::Vector<3> ev = gBno.getVector(Adafruit_BNO055::VECTOR_EULER);
    s->ex = (float)ev.x(); s->ey = (float)ev.y(); s->ez = (float)ev.z();
    imu::Vector<3> la = gBno.getVector(Adafruit_BNO055::VECTOR_LINEARACCEL);
    s->ax = (float)la.x(); s->ay = (float)la.y(); s->az = (float)la.z();
    gBno.getCalibration(&s->calSys, &s->calGyro, &s->calAccel, &s->calMag);
}

#if !PICKLE_PRODUCTION
/** Always prints to Serial; mirrors to SD when the logger mounted successfully. */
static void bootLog(const char *line) {
    SdLogger::serialPrintln(line);
}

static void bootLogf(const char *fmt, ...) {
    char buf[160];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    bootLog(buf);
}
#endif

static void renderIdleUi() {
    if (s_showPaddleIpUntilUeIdle && WiFi.status() == WL_CONNECTED) {
        const IPAddress ip = WiFi.localIP();
        if (ip != IPAddress(0, 0, 0, 0)) {
            char line2[22];
            snprintf(line2, sizeof(line2), "%s", ip.toString().c_str());
            gDisp.showTwoLines("Paddle IP", line2);
            return;
        }
    }
    gDisp.showTwoLines("Idle", "Ready");
}

static void drainUiEvents() {
    UiEventMsg ev;
    while (xQueueReceive(g_uiEventQueue, &ev, 0) == pdTRUE) {
        switch (ev.kind) {
        case UiEvent::SwingHitHost:
            if (SdLogger::instance().ok()) SdLogger::instance().log("event: swing hit (host)");
            triggerSwingHitAudioAsync();
            queueSwingHostFx();
            break;
        case UiEvent::ModeIdle:
            if (SdLogger::instance().ok()) SdLogger::instance().log("mode: idle");
            s_showPaddleIpUntilUeIdle = false;
            setRunMode(RunMode::Idle);
            applyWifiIdleRadio();
            renderIdleUi();
            break;
        case UiEvent::ModeGameplay:
            if (SdLogger::instance().ok()) SdLogger::instance().log("mode: gameplay");
            gJerk.configure(kGameplayJerkThreshold, kGameplayJerkRetriggerMs, kGameplayJerkLpfAlpha);
            setRunMode(RunMode::Gameplay);
            gJerk.reset();
            applyWifiStreamingBoost();
            gDisp.showTwoLines("Mode", "Gameplay");
            break;
        case UiEvent::ModeTutorial:
            if (SdLogger::instance().ok()) SdLogger::instance().log("mode: tutorial (flood)");
            gJerk.configure(kTutorialJerkThreshold, kTutorialJerkRetriggerMs, kTutorialJerkLpfAlpha);
            setRunMode(RunMode::Tutorial);
            gJerk.reset();
            applyWifiStreamingBoost();
            gDisp.showTwoLines("Mode", "Tutorial FLOOD");
            SdLogger::serialPrintln("[tutorial] entering flood mode — ex,ey,ez,btn,impulse");
            break;
        case UiEvent::SetIdleColor:
            // Always save the host-provided color for swing-hit feedback.
            gStrip.setSwingHitColor(ev.r, ev.g, ev.b);
            if (getRunMode() == RunMode::Idle) {
                gStrip.showSolidColor(ev.r, ev.g, ev.b);
                if (SdLogger::instance().ok()) {
                    SdLogger::instance().logf("idle color: r=%u g=%u b=%u",
                                              (unsigned)ev.r, (unsigned)ev.g, (unsigned)ev.b);
                }
            }
            break;
        }
    }
}

static void pollButton(RunMode mode) {
    static bool down = false;
    static uint32_t tDown = 0;
    static bool wifiForgetArmed = false;
    static bool stableRaw = false;
    static bool lastRawSample = false;
    static uint32_t rawChangedAt = 0;
    static bool immediateDownSent = false;

    const bool raw = (digitalRead(BUTTON_PIN) == LOW);
    const uint32_t now = millis();

    if (raw != lastRawSample) {
        lastRawSample = raw;
        rawChangedAt = now;
    }

    if (stableRaw != lastRawSample && (now - rawChangedAt) >= kButtonDebounceMs) {
        stableRaw = lastRawSample;
        if (stableRaw) {
            // Button pressed (debounced)
            down = true;
            tDown = now;
            wifiForgetArmed = false;
            // If we didn't already notify on raw edge, notify now.
            if (!immediateDownSent && mode != RunMode::Tutorial) {
                gNet.postText("detect btn down");
            }
        } else {
            // Button released (debounced)
            const uint32_t pressLen = now - tDown;
            down = false;
            wifiForgetArmed = false;
            if (mode != RunMode::Tutorial) {
                gNet.postText("detect btn up");
                // If it was a short tap, also send the legacy "push" event.
                if (pressLen < kButtonHoldMs) {
                    gNet.postText("detect btn push");
                }
            }
            // Clear immediate flag on release so next press will re-notify immediately.
            immediateDownSent = false;
        }
    }

    // Send immediate 'down' as soon as the raw sample shows a press (no debounce).
    if (lastRawSample != stableRaw) {
        // no-op: keep separate from immediate edge handler below
    }

    // Raw edge detection: when the instantaneous sample changes to pressed, notify immediately.
    // `lastRawSample` is updated earlier in the function when raw changes, so detect that transition.
    if (lastRawSample && !immediateDownSent) {
        // raw==pressed and we haven't reported it yet
        if (mode != RunMode::Tutorial) gNet.postText("detect btn down");
        immediateDownSent = true;
    }

    if (down) {
        // When held long enough in Idle mode, still trigger Wi-Fi forget.
        if (mode == RunMode::Idle && (now - tDown) >= kWifiForgetHoldMs && !wifiForgetArmed) {
            wifiForgetArmed = true;
            gDisp.showTwoLines("WiFi setup", "Forgetting...");
            SdLogger::serialPrintln("[prefs] Clearing Wi-Fi credentials — rebooting to setup portal.");
            Preferences prefs;
            prefs.begin(kPrefsNamespace, false);
            prefs.remove(kPrefsKeySsid);
            prefs.remove(kPrefsKeyPass);
            prefs.end();
            delay(400);
            ESP.restart();
        }
    }
}

/**
 * Tutorial flood loop — runs on core 1, reads IMU as fast as I²C allows and
 * fires a UDP packet each iteration.  Does not use gJerk: sharing the same
 * JerkDetector at ~kHz tutorial rates corrupts prevUs_/filter state and breaks
 * gameplay jerk + UDP after switching modes.  Impulse column = local jerk (m/s^3).
 * Exits back to the normal appTask loop when mode changes away from Tutorial.
 */
static void tutorialFloodLoop() {
    {   // Disable mux branches once (not per read).
        I2cBusLock lk;
        gMux.disableMuxBranches();
    }

    ImuSnapshot snap{};
    char pkt[128];
    // Local JerkDetector instance so tutorial sampling won't touch shared gJerk state.
    JerkDetector localJerk;
    localJerk.configure(kTutorialJerkThreshold, kTutorialJerkRetriggerMs, kTutorialJerkLpfAlpha);

    for (;;) {
        // Check for mode change (net task pushes events via queue).
        {
            UiEventMsg ev;
            while (xQueueReceive(g_uiEventQueue, &ev, 0) == pdTRUE) {
                switch (ev.kind) {
                case UiEvent::ModeIdle:
                    restoreImuWireAfterTutorialBurst();
                    s_showPaddleIpUntilUeIdle = false;
                    setRunMode(RunMode::Idle);
                    applyWifiIdleRadio();
                    renderIdleUi();
                    return;
                case UiEvent::ModeGameplay:
                    restoreImuWireAfterTutorialBurst();
                    gJerk.configure(kGameplayJerkThreshold, kGameplayJerkRetriggerMs, kGameplayJerkLpfAlpha);
                    setRunMode(RunMode::Gameplay);
                    gJerk.reset();
                    applyWifiStreamingBoost();
                    gDisp.showTwoLines("Mode", "Gameplay");
                    return;
                case UiEvent::ModeTutorial:
                    break; // already in tutorial
                case UiEvent::SwingHitHost:
                    triggerSwingHitAudioAsync();
                    queueSwingHostFx();
                    break;
                case UiEvent::SetIdleColor:
                    break; // ignore manual color commands in tutorial mode
                }
            }
        }

        // Fast button poll (no debounce logic — just sample state).
        const int btn = (digitalRead(BUTTON_PIN) == LOW) ? 0 : 1;

        if (!s_imuReady) {
            vTaskDelay(1);
            continue;
        }

        imuReadSnapshotFast(&snap);

        float impulse = 0.f;
        // Measure jerk from linear accel and always report the current magnitude
        // (no threshold gating for tutorial flood rows).
        // (do not gate on threshold in tutorial flood mode).
        {
            Vec3 a{snap.ax, snap.ay, snap.az};
            (void)localJerk.update(a, micros());
            impulse = localJerk.lastJerkMagnitude();
        }

        (void)snprintf(pkt, sizeof(pkt),
                       "%.1f,%.1f,%.1f,%d,%.1f",
                       snap.ex, snap.ey, snap.ez,
                       btn, impulse);

        (void)gNet.postText(pkt);

        // Keep tutorial very fast while avoiding an unrestricted flood.
        if (kTutorialImuPeriodMs > 0) {
            vTaskDelay(pdMS_TO_TICKS(kTutorialImuPeriodMs));
        } else {
            taskYIELD();
        }
    }
}

static void appTask(void * /*param*/) {
    gJerk.configure(kGameplayJerkThreshold, kGameplayJerkRetriggerMs, kGameplayJerkLpfAlpha);
    uint32_t lastImu = 0;

    for (;;) {
#if !PICKLE_PRODUCTION
        static uint32_t s_lastDropCount = 0;
        static uint32_t s_lastDropLogMs = 0;
        const uint32_t drops = g_netUiEnqueueDrops;
        if (drops != s_lastDropCount) {
            const uint32_t delta = drops - s_lastDropCount;
            s_lastDropCount = drops;
            const uint32_t t = millis();
            if (t - s_lastDropLogMs >= 400u || delta >= 3u) {
                s_lastDropLogMs = t;
                SdLogger::serialPrintf(
                    "[net] UDP→UI queue dropped %lu (total %lu) — PC commands may be missed\n",
                    (unsigned long)delta, (unsigned long)drops);
            }
        }
#endif
        drainUiEvents();

        const RunMode mode = getRunMode();
        pollButton(mode);

        if (mode == RunMode::Tutorial && s_imuReady) {
            tutorialFloodLoop();
            lastImu = 0;
            continue;
        }

        if (s_imuReady && mode == RunMode::Gameplay) {
            const uint32_t now = millis();
            if (now - lastImu >= kGameplayImuPeriodMs) {
                lastImu = now;
                Vec3 a, euler;
                imuReadLinearEuler(&a, &euler);
                (void)euler;
                // Same jerk estimate as tutorial (JerkDetector); threshold + retrigger are inside update().
                if (gJerk.update(a, micros())) {
                    const float impulse = gJerk.lastJerkMagnitude();
                    char pkt[24];
                    (void)snprintf(pkt, sizeof(pkt), "%.1f", impulse);
#if !PICKLE_PRODUCTION
                    SdLogger::serialPrintf(
                        "[imu] tx jerk m/s^3 x=%.3f y=%.3f z=%.3f  |jerk|=%.1f\n",
                        a.x, a.y, a.z, impulse);
                    if (SdLogger::instance().ok()) {
                        char jbuf[32];
                        snprintf(jbuf, sizeof(jbuf), "%.1f", impulse);
                        SdLogger::instance().logf("impulse tx %s", jbuf);
                    }
#endif
                    (void)gNet.postText(pkt);
                    queueGameplaySwingFx();
                }
            }
        }

        vTaskDelay(1);
    }
}

static void netTask(void * /*param*/) {
    for (;;) {
        gNet.service();
        vTaskDelay(1);
    }
}

static void bootPlayStartupFx() {
#if !PICKLE_PRODUCTION
    gDisp.showTwoLines("Device initialized.", "Startup FX...");
    bootLog("boot: startup FX");
#endif
    paddleFx_playSteps(gMux, kFxBootHaptic, PADDLE_FX_STEP_COUNT(kFxBootHaptic));
    paddleFx_playSpeakerSteps(gSpk, kFxBootSpeaker, PADDLE_FX_SPEAKER_COUNT(kFxBootSpeaker));
    gStrip.playBootSequence();
}

static void bootProbeSequence() {
#if PICKLE_PRODUCTION
    (void)gStrip.begin();
    gMux.disableMuxBranches();
    delay(30);
    s_imuReady = imuBeginAdafruit();
    {
        I2cBusLock lk;
        Wire.begin(BUS_SDA, BUS_SCL);
        Wire.setClock(100000);
        Wire.setTimeOut(200);
        delay(10);
    }
    (void)gMux.scanDrivers();
    (void)gSpk.probe(SPEAKER_PWM, SPEAKER_LEDC_CHAN, SPEAKER_LEDC_BITS);
#else
    gDisp.showTwoLines("Initializing Device...", "");
    bootLog("boot: start probes");

    const bool ledOk = gStrip.begin();
    gDisp.showTwoLines("NeoPixel", ledOk ? "ok" : "no strip [X]");
    bootLogf("probe NeoPixel %s", ledOk ? "ok" : "FAIL");

    // BNO055 before TCA/DRV scan: Adafruit begin() polls until the chip ACKs; mux traffic
    // first caused many failed detections and Wire error spam on each retry.
    gMux.disableMuxBranches();
    delay(30);

    const bool imuOk = imuBeginAdafruit();
    s_imuReady = imuOk;
    gDisp.showTwoLines("IMU", imuOk ? "ok" : "unable to connect IMU [X]");
    bootLogf("probe IMU (Adafruit) %s  addr=0x%02X  %lu Hz", imuOk ? "ok" : "FAIL",
             (unsigned)kBno055I2cAddr, (unsigned long)kImuI2cHz);

    if (imuOk) {
        bootLog("imu: startup linear read test (5 samples, 100ms apart)");
        for (int i = 0; i < 5; i++) {
            Vec3 a;
            imuReadLinear(&a);
            bootLogf("imu: sample %d  %.2f %.2f %.2f m/s^2", i + 1, a.x, a.y, a.z);
            delay(100);
        }
    }

    // Wire bus recovery: the BNO055 init above generates many i2cWriteReadNonStop Error -1
    // which can leave the ESP32 Wire peripheral in a bad state. Re-init the bus so the haptic
    // scan talks to a clean peripheral.
    {
        I2cBusLock lk;
        Wire.begin(BUS_SDA, BUS_SCL);
        Wire.setClock(100000);
        Wire.setTimeOut(200);
        delay(10);
    }

    const int hCount = gMux.scanDrivers();
    if (hCount == 0) {
        gDisp.showTwoLines("Haptics", "# of haptic found: 0 [X]");
    } else {
        char line[32];
        snprintf(line, sizeof(line), "found: %d", hCount);
        gDisp.showTwoLines("Haptics", line);
    }
    bootLogf("probe haptics count=%d", hCount);
    if (hCount > 0) {
        gDisp.showTwoLines("Haptics", "Testing each motor...");
        bootLog("haptics: sequential startup test (one mux branch at a time)");
        gMux.bootSequenceVibrate();
    }

    const bool spkOk = gSpk.probe(SPEAKER_PWM, SPEAKER_LEDC_CHAN, SPEAKER_LEDC_BITS);
    gDisp.showTwoLines("Speaker", spkOk ? "ok" : "unable to connect Speaker [X]");
    bootLogf("probe speaker %s", spkOk ? "ok" : "FAIL");
#endif
    bootPlayStartupFx();
}

void setup() {
    Serial.begin(115200);
    delay(200);

    i2cBusMutexInit();
    pinMode(BUTTON_PIN, INPUT_PULLUP);

    g_stateMutex = xSemaphoreCreateMutex();
    g_uiEventQueue = xQueueCreate(kUiEventQueueDepth, sizeof(UiEventMsg));
    // Tutorial can stream ~150+ rows/s; larger queue so postText rarely times out under load.
    g_netTxQueue = xQueueCreate(160, sizeof(NetOutgoingMsg));
    g_swingFxQueue = xQueueCreate(kSwingFxQueueDepth, sizeof(uint8_t));

    gMux.beginWire(BUS_SDA, BUS_SCL, 100000);

    if (!gMux.probeMux(TCA9548A_ADDR)) {
        SdLogger::serialPrintln(
            "[I2C] TCA9548A not detected by probe; still driving 0x70 for haptic channels.");
    }
    gMux.disableMuxBranches();

    if (!gDisp.begin()) {
        SdLogger::serialPrintln("OLED (0.91) init failed.");
    }

    // Run UI / I2C probes before SD: no card must not block or reset before the display test.
    bootProbeSequence();

    if (!SdLogger::instance().begin(kSdLogPath)) {
        SdLogger::serialPrintln("[boot] SD unavailable — continuing without file logging.");
    } else {
        SdLogger::serialPrintf("[boot] SD log path: %s\n", SdLogger::instance().path());
        const esp_reset_reason_t rr = esp_reset_reason();
        SdLogger::serialPrintf("[boot] reset reason: %d (brownout=%d)\n", (int)rr,
                               (int)(rr == ESP_RST_BROWNOUT));
    }

    Preferences prefs;
    prefs.begin(kPrefsNamespace, true);
    const String ssid = prefs.getString(kPrefsKeySsid, "");
    prefs.end();

    if (ssid.length() == 0) {
        gDisp.showTwoLines("WiFi setup", "AP: PicklePaddle-Setup");
        gPortal.runBlockingSetupPortal(&gDisp, &gStrip);
        delay(200);
        ESP.restart();
    }

    hostAddrLoadFromPrefs();

    gDisp.showTwoLines("Connecting...", ssid.c_str());

    if (!gPortal.connectSta(&gDisp, &gStrip)) {
        s_showPaddleIpUntilUeIdle = false;
        gDisp.showTwoLines("WiFi", "Unable to connect to wifi. [X]");
        if (SdLogger::instance().ok()) SdLogger::instance().log("wifi: connect FAILED");
        delay(2500);
    } else {
        // Ramp already raised TX in connectSta; short settle then one idle-radio apply (PS + TX).
        delay(kWifiPostConnectSettleMs);
        applyWifiIdleRadio();
        gStrip.showStaConnectedSolid();
        char line[44];
        snprintf(line, sizeof(line), "IP: %s", WiFi.localIP().toString().c_str());
        gDisp.showTwoLines("Wifi Connected.", line);
        if (SdLogger::instance().ok())
            SdLogger::instance().logf("wifi: ok %s", WiFi.localIP().toString().c_str());
        delay(800);
    }

    if (!gNet.begin(kLocalUdpPort)) {
        SdLogger::serialPrintln("UDP begin failed");
        if (SdLogger::instance().ok()) SdLogger::instance().log("udp: begin FAILED");
    } else if (SdLogger::instance().ok()) {
        SdLogger::instance().logf("udp: listen port %u", (unsigned)kLocalUdpPort);
    }
    if (g_hostAddr != IPAddress(0, 0, 0, 0) && g_hostPort != 0) {
        gNet.setRemote(g_hostAddr, g_hostPort);
    }

    setRunMode(RunMode::Idle);
    renderIdleUi();

    constexpr uint32_t kNetStack = 8192;
    constexpr uint32_t kAppStack = 10240;
    xTaskCreatePinnedToCore(netTask, "net", kNetStack, nullptr, kNetTaskPriority, nullptr, 0);
    xTaskCreatePinnedToCore(appTask, "app", kAppStack, nullptr, kAppTaskPriority, nullptr, 1);
    if (g_swingFxQueue) {
        xTaskCreatePinnedToCore(swingFxWorkerTask, "swing_fx", kSwingFxTaskStackBytes, nullptr,
                                kSwingFxTaskPriority, nullptr, 0);
    }
}

void loop() {
    vTaskDelay(pdMS_TO_TICKS(1000));
}

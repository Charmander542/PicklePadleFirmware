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

/** Highest throughput / range for UDP streaming (gameplay & tutorial). AP must support HT40 for 40 MHz. */
static void applyWifiStreamingBoost() {
    if (WiFi.status() != WL_CONNECTED) return;

    WiFi.setSleep(false);
    WiFi.setTxPower(mapDbmToWifiPower(kWifiStreamingTxPowerDbm));
    (void)esp_wifi_set_ps(WIFI_PS_NONE);
    (void)esp_wifi_set_protocol(WIFI_IF_STA,
                                WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N);
    if (esp_wifi_set_bandwidth(WIFI_IF_STA, WIFI_BW_HT40) != ESP_OK) {
        (void)esp_wifi_set_bandwidth(WIFI_IF_STA, WIFI_BW_HT20);
    }
}

/** Restore calmer radio when returning to idle (saves power / neighbor-friendlier than HT40). */
static void applyWifiIdleRadio() {
    if (WiFi.status() != WL_CONNECTED) return;

    WiFi.setSleep(false);
    WiFi.setTxPower(mapDbmToWifiPower(kWifiRunTxPowerDbm));
    (void)esp_wifi_set_ps(WIFI_PS_NONE);
    (void)esp_wifi_set_bandwidth(WIFI_IF_STA, WIFI_BW_HT20);
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

/** Euler angles in degrees (BNO055: heading, roll, pitch). */
static void imuReadEuler(Vec3 *out) {
    I2cBusLock lk;
    gMux.disableMuxBranches();
    Wire.setTimeOut(kImuWireTimeoutMs);
    Wire.setClock(kImuReadClockHz);
    imu::Vector<3> ev = gBno.getVector(Adafruit_BNO055::VECTOR_EULER);
    out->x = (float)ev.x();
    out->y = (float)ev.y();
    out->z = (float)ev.z();
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
            paddleFx_playSteps(gMux, kFxBallHitHaptic, PADDLE_FX_STEP_COUNT(kFxBallHitHaptic));
            gStrip.playBallHit();
            paddleFx_playSpeakerSteps(gSpk, kFxBallHitSpeaker,
                                      PADDLE_FX_SPEAKER_COUNT(kFxBallHitSpeaker));
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
            gJerk.configure(kGameplayJerkThreshold, kGameplayJerkRetriggerMs, kGameplayJerkLpfAlpha);
            setRunMode(RunMode::Tutorial);
            gJerk.reset();
            applyWifiStreamingBoost();
            gDisp.showTwoLines("Mode", "Tutorial FLOOD");
            SdLogger::serialPrintln("[tutorial] entering flood mode — ex,ey,ez,btn,impulse");
            break;
        }
    }
}

static void pollButton(RunMode mode) {
    static bool down = false;
    static uint32_t tDown = 0;
    static bool holdSent = false;
    static bool wifiForgetArmed = false;

    const bool raw = (digitalRead(BUTTON_PIN) == LOW);
    const uint32_t now = millis();

    if (raw) {
        if (!down) {
            down = true;
            tDown = now;
            holdSent = false;
            wifiForgetArmed = false;
        } else {
            if (!holdSent && (now - tDown) >= kButtonHoldMs) {
                holdSent = true;
                if (mode != RunMode::Tutorial) gNet.postText("detect btn hold");
            }
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
    } else {
        if (down) {
            const uint32_t elapsed = now - tDown;
            if (!holdSent && elapsed >= kButtonDebounceMs && elapsed < kButtonHoldMs) {
                if (mode != RunMode::Tutorial) gNet.postText("detect btn push");
            }
        }
        down = false;
        holdSent = false;
        wifiForgetArmed = false;
    }
}

/**
 * Tutorial flood loop — runs on core 1, reads IMU as fast as I²C allows and
 * fires a UDP packet each iteration.  No serial print, no SD log, no jerk
 * detection, no queue — just imuReadSnapshotFast → snprintf → sendFast.
 * Button state is polled every iteration (digitalRead is ~0.5 µs on ESP32).
 * Exits back to the normal appTask loop when mode changes away from Tutorial.
 */
static void tutorialFloodLoop() {
    {   // Disable mux branches once (not per read).
        I2cBusLock lk;
        gMux.disableMuxBranches();
    }

    ImuSnapshot snap{};
    char pkt[128];

    for (;;) {
        // Check for mode change (net task pushes events via queue).
        {
            UiEventMsg ev;
            while (xQueueReceive(g_uiEventQueue, &ev, 0) == pdTRUE) {
                switch (ev.kind) {
                case UiEvent::ModeIdle:
                    s_showPaddleIpUntilUeIdle = false;
                    setRunMode(RunMode::Idle);
                    applyWifiIdleRadio();
                    renderIdleUi();
                    return;
                case UiEvent::ModeGameplay:
                    gJerk.configure(kGameplayJerkThreshold, kGameplayJerkRetriggerMs, kGameplayJerkLpfAlpha);
                    setRunMode(RunMode::Gameplay);
                    gJerk.reset();
                    applyWifiStreamingBoost();
                    gDisp.showTwoLines("Mode", "Gameplay");
                    return;
                case UiEvent::ModeTutorial:
                    break; // already in tutorial
                case UiEvent::SwingHitHost:
                    break; // ignore FX in flood mode
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

        Vec3 accel = {snap.ax, snap.ay, snap.az};
        const bool jerkTriggered = gJerk.update(accel, micros());
        const float impulse = jerkTriggered ? gJerk.lastJerkMagnitude() : 0.0f;

        const int len = snprintf(pkt, sizeof(pkt),
            "%.1f,%.1f,%.1f,%d,%.1f",
            snap.ex, snap.ey, snap.ez,
            btn, impulse);

        gNet.sendFast(pkt, (uint16_t)len);

        // Minimal yield so the RTOS can service WiFi and the net task can handle RX.
        taskYIELD();
    }
}

static void appTask(void * /*param*/) {
    gJerk.configure(kGameplayJerkThreshold, kGameplayJerkRetriggerMs, kGameplayJerkLpfAlpha);
    uint32_t lastImu = 0;

    for (;;) {
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
                Vec3 a;
                imuReadLinear(&a);
                const uint32_t nowUs = micros();
                if (gJerk.update(a, nowUs)) {
                    const float j = gJerk.lastJerkMagnitude();
                    SdLogger::serialPrintf(
                        "[imu] tx linear_accel m/s^2 x=%.3f y=%.3f z=%.3f  jerk=%.1f m/s^3\n",
                        a.x, a.y, a.z, j);
                    char buf[48];
                    snprintf(buf, sizeof(buf), "%.1f", j);
                    if (SdLogger::instance().ok())
                        SdLogger::instance().logf("impulse tx %s", buf);
                    if (!gNet.trySendImmediate(buf)) {
                        gNet.postText(buf);
                    }
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
    g_uiEventQueue = xQueueCreate(16, sizeof(UiEventMsg));
    // Tutorial can stream ~150+ rows/s; larger queue so postText rarely times out under load.
    g_netTxQueue = xQueueCreate(160, sizeof(NetOutgoingMsg));

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
        // Let modem sleep + low TX stay through connect/ramp before another current step.
        delay(kWifiPowerRampStepDelayMs);
        WiFi.setSleep(false);  // modem sleep can starve I2C / CPU timing under load
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
}

void loop() {
    vTaskDelay(pdMS_TO_TICKS(1000));
}

#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>
#include <stdio.h>

#include "pins.h"
#include "app_config.h"
#include "app_state.h"
#include "jerk_detect.h"
#include "paddle_fx.h"

#include <BNO055Fast.h>
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
static BNO055Fast gImu(&Wire);
static JerkDetector gJerk;
static NetUdp gNet;
static WifiPortal gPortal;
static bool s_imuReady = false;

static void renderIdleUi() { gDisp.showTwoLines("Idle", "Ready"); }

static void drainUiEvents() {
    UiEventMsg ev;
    while (xQueueReceive(g_uiEventQueue, &ev, 0) == pdTRUE) {
        switch (ev.kind) {
        case UiEvent::SwingHitHost:
            if (SdLogger::instance().ok()) SdLogger::instance().log("event: swing hit (host)");
            paddleFx_playSteps(gMux, kFxBallHitHaptic, PADDLE_FX_STEP_COUNT(kFxBallHitHaptic));
            gStrip.playBallHit();
            gSpk.playBallHit();
            break;
        case UiEvent::ModeIdle:
            if (SdLogger::instance().ok()) SdLogger::instance().log("mode: idle");
            setRunMode(RunMode::Idle);
            renderIdleUi();
            break;
        case UiEvent::ModeGameplay:
            if (SdLogger::instance().ok()) SdLogger::instance().log("mode: gameplay");
            setRunMode(RunMode::Gameplay);
            gJerk.reset();
            gDisp.showTwoLines("Mode", "Gameplay");
            break;
        }
    }
}

static void pollButton(RunMode mode) {
    (void)mode;
    static bool down = false;
    static uint32_t tDown = 0;
    static bool holdSent = false;

    const bool raw = (digitalRead(BUTTON_PIN) == LOW);
    const uint32_t now = millis();

    if (raw) {
        if (!down) {
            down = true;
            tDown = now;
            holdSent = false;
        } else if (!holdSent && (now - tDown) >= kButtonHoldMs) {
            holdSent = true;
            gNet.postText("detect btn hold");
        }
    } else {
        if (down) {
            const uint32_t elapsed = now - tDown;
            if (!holdSent && elapsed >= kButtonDebounceMs && elapsed < kButtonHoldMs) {
                gNet.postText("detect btn push");
            }
        }
        down = false;
        holdSent = false;
    }
}

static void appTask(void * /*param*/) {
    gJerk.configure(kGameplayJerkThreshold, kGameplayJerkRetriggerMs, 0.2f);
    uint32_t lastImu = 0;

    for (;;) {
        drainUiEvents();

        const RunMode mode = getRunMode();
        pollButton(mode);

        if (mode == RunMode::Gameplay && s_imuReady) {
            const uint32_t now = millis();
            if (now - lastImu >= kImuPeriodMs) {
                lastImu = now;
                if (gImu.readLinearAccel()) {
                    const Vec3 a = gImu.getLinearAccel();
                    if (gJerk.update(a, now)) {
                        char buf[48];
                        snprintf(buf, sizeof(buf), "%.1f", gJerk.lastJerkMagnitude());
                        if (SdLogger::instance().ok())
                            SdLogger::instance().logf("impulse tx %s", buf);
                        gNet.postText(buf);
                    }
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(4));
    }
}

static void netTask(void * /*param*/) {
    for (;;) {
        gNet.service();
        vTaskDelay(pdMS_TO_TICKS(2));
    }
}

static void bootProbeSequence() {
    gDisp.showTwoLines("Initializing Device...", "");
    if (SdLogger::instance().ok()) SdLogger::instance().log("boot: start probes");

    const bool ledOk = gStrip.begin();
    gDisp.showTwoLines("NeoPixel", ledOk ? "ok" : "no strip [X]");
    if (SdLogger::instance().ok()) SdLogger::instance().logf("probe NeoPixel %s", ledOk ? "ok" : "FAIL");

    const bool imuOk = gImu.begin(400000);
    s_imuReady = imuOk;
    gDisp.showTwoLines("IMU", imuOk ? "ok" : "unable to connect IMU [X]");
    if (SdLogger::instance().ok()) SdLogger::instance().logf("probe IMU %s", imuOk ? "ok" : "FAIL");

    const int hCount = gMux.scanDrivers();
    if (hCount == 0) {
        gDisp.showTwoLines("Haptics", "# of haptic found: 0 [X]");
    } else {
        char line[32];
        snprintf(line, sizeof(line), "found: %d", hCount);
        gDisp.showTwoLines("Haptics", line);
    }
    if (SdLogger::instance().ok()) SdLogger::instance().logf("probe haptics count=%d", hCount);

    const bool spkOk = gSpk.probe(SPEAKER_PWM, SPEAKER_LEDC_CHAN, SPEAKER_LEDC_BITS);
    gDisp.showTwoLines("Speaker", spkOk ? "ok" : "unable to connect Speaker [X]");
    if (SdLogger::instance().ok()) SdLogger::instance().logf("probe speaker %s", spkOk ? "ok" : "FAIL");

    gDisp.showTwoLines("Device initialized.", "Startup FX...");
    if (SdLogger::instance().ok()) SdLogger::instance().log("boot: startup FX");

    paddleFx_playSteps(gMux, kFxBootHaptic, PADDLE_FX_STEP_COUNT(kFxBootHaptic));
    gSpk.playBootRhythm();
    gStrip.playBootSequence();
}

void setup() {
    Serial.begin(115200);
    delay(200);

    pinMode(BUTTON_PIN, INPUT_PULLUP);

    g_stateMutex = xSemaphoreCreateMutex();
    g_uiEventQueue = xQueueCreate(16, sizeof(UiEventMsg));
    g_netTxQueue = xQueueCreate(40, sizeof(NetOutgoingMsg));

    gMux.beginWire(BUS_SDA, BUS_SCL, 100000);

    if (!gMux.probeMux(TCA9548A_ADDR)) {
        Serial.println("TCA9548A not found.");
    }

    if (!gDisp.begin()) {
        Serial.println("OLED (0.91) init failed.");
    }

    if (!SdLogger::instance().begin()) {
        Serial.println("SD log disabled (no card or mount error).");
    }

    bootProbeSequence();

    Preferences prefs;
    prefs.begin(kPrefsNamespace, true);
    const String ssid = prefs.getString(kPrefsKeySsid, "");
    prefs.end();

    if (ssid.length() == 0) {
        gDisp.showTwoLines("WiFi setup", "AP: PicklePaddel-Setup");
        gPortal.runBlockingSetupPortal(&gDisp);
        delay(200);
        ESP.restart();
    }

    hostAddrLoadFromPrefs();

    gDisp.showTwoLines("Connecting...", ssid.c_str());

    if (!gPortal.connectSta(&gDisp)) {
        gDisp.showTwoLines("WiFi", "Unable to connect to wifi. [X]");
        if (SdLogger::instance().ok()) SdLogger::instance().log("wifi: connect FAILED");
        delay(2500);
    } else {
        char line[44];
        snprintf(line, sizeof(line), "IP: %s", WiFi.localIP().toString().c_str());
        gDisp.showTwoLines("Wifi Connected.", line);
        if (SdLogger::instance().ok())
            SdLogger::instance().logf("wifi: ok %s", WiFi.localIP().toString().c_str());
        delay(800);
    }

    if (!gNet.begin(kLocalUdpPort)) {
        Serial.println("UDP begin failed");
        if (SdLogger::instance().ok()) SdLogger::instance().log("udp: begin FAILED");
    } else if (SdLogger::instance().ok()) {
        SdLogger::instance().logf("udp: listen port %u", (unsigned)kLocalUdpPort);
    }
    if (g_hostAddr != IPAddress(0, 0, 0, 0)) {
        gNet.setRemote(g_hostAddr, g_hostPort);
    }

    setRunMode(RunMode::Idle);
    renderIdleUi();

    constexpr uint32_t kNetStack = 8192;
    constexpr uint32_t kAppStack = 10240;
    xTaskCreatePinnedToCore(netTask, "net", kNetStack, nullptr, 1, nullptr, 0);
    xTaskCreatePinnedToCore(appTask, "app", kAppStack, nullptr, 1, nullptr, 1);
}

void loop() {
    vTaskDelay(pdMS_TO_TICKS(1000));
}

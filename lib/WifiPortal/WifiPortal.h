#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include "app_config.h"

class DisplayManager;

class WifiPortal {
public:
    // AP + captive DNS + config web. Blocks until client submits form; saves NVS and returns true (caller should restart).
    bool runBlockingSetupPortal(DisplayManager *disp);

    // STA with stored credentials; optional open-network fallback.
    bool connectSta(DisplayManager *disp);

private:
    void handleRoot_();
    void handleSave_();
    void handleCaptive_();

    bool saveRequested_{false};
    WebServer server_{80};
    DNSServer dns_;
    Preferences prefs_;
    DisplayManager *disp_{nullptr};
    IPAddress apIP_{192, 168, 4, 1};
};

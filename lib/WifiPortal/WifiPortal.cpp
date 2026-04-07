#include "WifiPortal.h"
#include "DisplayManager.h"
#include "NeoPixelStrip.h"

static const char kHtml[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html><head><meta name="viewport" content="width=device-width"/>
<title>PicklePaddle WiFi</title></head><body>
<h2>PicklePaddle setup</h2>
<form method="POST" action="/save">
<p>SSID<br><input name="ssid" value="BU Guest (unencrypted)" style="width:90%"/></p>
<p>Password (empty for open)<br><input name="pass" type="password" style="width:90%"/></p>
<p>Host Computer IP (game PC)<br><input name="host" value="10.193." style="width:90%"/></p>
<p>Host UDP port<br><input name="port" value="4210" style="width:90%"/></p>
<button type="submit">Save &amp; reboot</button>
</form></body></html>
)rawliteral";

static wifi_power_t mapDbmToWifiPower_(int8_t dbm) {
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

void WifiPortal::handleRoot_() {
    server_.send_P(200, "text/html", kHtml);
}

void WifiPortal::handleSave_() {
    String ssid = server_.hasArg("ssid") ? server_.arg("ssid") : "";
    String pass = server_.hasArg("pass") ? server_.arg("pass") : "";
    String host = server_.hasArg("host") ? server_.arg("host") : "";
    String port = server_.hasArg("port") ? server_.arg("port") : "";

    prefs_.begin(kPrefsNamespace, false);
    prefs_.putString(kPrefsKeySsid, ssid);
    prefs_.putString(kPrefsKeyPass, pass);
    if (host.length()) prefs_.putString(kPrefsKeyHostIp, host);
    prefs_.putUInt(kPrefsKeyHostPort, port.length() ? port.toInt() : kDefaultHostPort);
    prefs_.end();

    server_.send(200, "text/html", "<html><body>Saved. Rebooting...</body></html>");
    saveRequested_ = true;
}

void WifiPortal::handleCaptive_() {
    server_.sendHeader("Location", String("http://") + apIP_.toString(), true);
    server_.send(302, "text/plain", "");
}

bool WifiPortal::runBlockingSetupPortal(DisplayManager *disp, NeoPixelStrip *leds) {
    disp_ = disp;
    leds_ = leds;
    saveRequested_ = false;
    if (leds_) {
        leds_->resetWifiLedAnim();
    }

    WiFi.mode(WIFI_AP);
    WiFi.softAPConfig(apIP_, apIP_, IPAddress(255, 255, 255, 0));
    if (strlen(kPortalPass) > 0)
        WiFi.softAP(kPortalSsid, kPortalPass);
    else
        WiFi.softAP(kPortalSsid);

    dns_.start(53, "*", apIP_);

    server_.on("/", HTTP_GET, [this]() { handleRoot_(); });
    server_.on("/save", HTTP_POST, [this]() { handleSave_(); });
    server_.on("/generate_204", HTTP_GET, [this]() { handleCaptive_(); });
    server_.on("/fwlink", HTTP_GET, [this]() { handleCaptive_(); });
    server_.onNotFound([this]() { handleCaptive_(); });
    server_.begin();

    if (disp_) {
        disp_->showTwoLines("Setup WiFi", "Join: PicklePaddle-Setup");
    }

    while (!saveRequested_) {
        dns_.processNextRequest();
        server_.handleClient();
        if (leds_) {
            leds_->tickApPortal(WiFi.softAPgetStationNum() > 0);
        }
        delay(5);
    }
    server_.stop();
    dns_.stop();
    delay(300);
    return true;
}

bool WifiPortal::connectSta(DisplayManager *disp, NeoPixelStrip *leds) {
    prefs_.begin(kPrefsNamespace, true);
    String ssid = prefs_.getString(kPrefsKeySsid, "");
    String pass = prefs_.getString(kPrefsKeyPass, "");
    prefs_.end();

    if (ssid.length() == 0) return false;

    if (leds) {
        leds->resetWifiLedAnim();
    }

    WiFi.mode(WIFI_STA);
    WiFi.persistent(false);
    WiFi.setAutoReconnect(true);
    WiFi.setSleep(true);  // lower instantaneous draw during association
    WiFi.setTxPower(mapDbmToWifiPower_(kWifiConnectTxPowerDbm));
    delay(40);
    WiFi.begin(ssid.c_str(), pass.c_str());

    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 20000) {
        if (leds) {
            leds->tickStaConnecting();
        }
        delay(80);
    }

    if (WiFi.status() == WL_CONNECTED) {
        return true;
    }

    if (strcmp(kFallbackSsid, ssid.c_str()) != 0) {
        if (disp) disp->showTwoLines("Retry open", kFallbackSsid);
        WiFi.disconnect();
        delay(200);
        if (leds) {
            leds->resetWifiLedAnim();
        }
        WiFi.setTxPower(mapDbmToWifiPower_(kWifiConnectTxPowerDbm));
        delay(40);
        WiFi.begin(kFallbackSsid);
        start = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
            if (leds) {
                leds->tickStaConnecting();
            }
            delay(80);
        }
    }

    const bool ok = WiFi.status() == WL_CONNECTED;
    if (ok) {
        delay(kWifiPowerRampDelayMs);
        WiFi.setTxPower(mapDbmToWifiPower_(kWifiRunTxPowerDbm));
    }
    if (!ok && leds) {
        leds->resetWifiLedAnim();
        leds->clear();
    }
    return ok;
}

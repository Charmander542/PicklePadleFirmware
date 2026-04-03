#include "SdLogger.h"

#include "app_config.h"
#include "pins.h"
#include <Arduino.h>
#include <FS.h>
#include <SD.h>
#include <SPI.h>
#include <esp_task_wdt.h>
#include <stdio.h>
#include <string.h>

namespace {

/** SD.begin can block longer than the Arduino loop task TWDT; that yields reset/boot-loop. */
struct TaskWdtSuspendForSdMount {
    bool active_{false};
    TaskWdtSuspendForSdMount() {
        if (esp_task_wdt_delete(nullptr) == ESP_OK) {
            active_ = true;
        }
    }
    ~TaskWdtSuspendForSdMount() {
        if (active_) {
            (void)esp_task_wdt_add(nullptr);
        }
    }
};

}  // namespace

SdLogger &SdLogger::instance() {
    static SdLogger inst;
    return inst;
}

bool SdLogger::begin(const char *path) {
    if (giveUp_) {
        return false;
    }
    if (ok_) {
        return true;
    }

#if !ENABLE_SD_FILE_LOG
    giveUp_ = true;
    ok_ = false;
    Serial.println("[SD] Disabled (ENABLE_SD_FILE_LOG=0) — no SPI/SD init.");
    return false;
#endif

    if (mu_ == nullptr) {
        mu_ = xSemaphoreCreateMutex();
        if (!mu_) {
            giveUp_ = true;
            return false;
        }
    }

    strncpy(path_, path ? path : "/paddle.log", sizeof(path_) - 1);
    path_[sizeof(path_) - 1] = 0;

    TaskWdtSuspendForSdMount twdt_sd;

    // Do not call SPI.end/SD.end before first probe — cold boot + no card has caused
    // unstable behavior on some boards. SS = -1 so SD library owns CS as GPIO only.
    SPI.begin((int8_t)SD_CLK, (int8_t)SD_MISO, (int8_t)SD_MOSI, -1);
    yield();

    // 400 kHz: fail fast on empty/floating lines; higher rates can stall longer.
    if (!SD.begin((uint8_t)SD_CS, SPI, 400000)) {
        Serial.printf(
            "[SD] No card or mount failed — logging disabled (SPI released). "
            "CLK=%d MISO=%d MOSI=%d CS=%d\r\n",
            (int)SD_CLK, (int)SD_MISO, (int)SD_MOSI, (int)SD_CS);
        // Do not call SD.end() here: after a failed begin() the SD/VFS layer can be in a bad
        // state and end() triggers LoadProhibited / reboot (seen with no card inserted).
        SPI.end();
        yield();
        delay(10);
        giveUp_ = true;
        ok_ = false;
        return false;
    }

    if (SD.cardType() == CARD_NONE) {
        Serial.println("[SD] cardType NONE after begin — disabling SD.");
        SD.end();
        SPI.end();
        yield();
        delay(10);
        giveUp_ = true;
        ok_ = false;
        return false;
    }

    ok_ = true;
    log("sd: logger ready");
    return true;
}

void SdLogger::shutdown() {
    if (mu_ == nullptr) return;
    if (xSemaphoreTake(mu_, pdMS_TO_TICKS(2000)) == pdTRUE) {
        const bool had_mount = ok_;
        ok_ = false;
        if (had_mount) {
            SD.end();
        }
        xSemaphoreGive(mu_);
    }
}

void SdLogger::log(const char *line) {
    if (giveUp_ || !ok_ || !line) return;
    if (xSemaphoreTake(mu_, pdMS_TO_TICKS(500)) != pdTRUE) return;

    if (SD.cardType() == CARD_NONE) {
        ok_ = false;
        Serial.println("[SD] Card removed — file logging stopped.");
        xSemaphoreGive(mu_);
        return;
    }

    File f = SD.open(path_, FILE_APPEND);
    if (f) {
        f.print(millis());
        f.print(" ms ");
        f.println(line);
        f.close();
    } else {
        ok_ = false;
        Serial.printf("[SD] open \"%s\" failed — file logging stopped.\r\n", path_);
    }
    xSemaphoreGive(mu_);
}

void SdLogger::logf(const char *fmt, ...) {
    if (giveUp_ || !ok_) return;
    char buf[240];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    log(buf);
}

#include "SdLogger.h"

#include "app_config.h"
#include "pins.h"
#include <Arduino.h>
#include <FS.h>
#include <SD.h>
#include <SPI.h>
#include <esp_random.h>
#include <esp_task_wdt.h>
#include <stdio.h>
#include <string.h>

namespace {

/** ESP-IDF: CONFIG_FATFS_LFN_NONE — only 8.3 short names (≤8 base + '.' + ≤3 ext). */
static bool shortNameLooks83(const char *path) {
    if (!path || path[0] != '/') return false;
    const char *slash = strrchr(path, '/');
    const char *base = slash ? slash + 1 : path;
    if (*base == 0) return false;
    const char *dot = strrchr(base, '.');
    if (!dot || dot == base) return false;
    const size_t nBase = (size_t)(dot - base);
    const size_t nExt = strlen(dot + 1);
    return nBase >= 1 && nBase <= 8 && nExt >= 1 && nExt <= 3;
}

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

    TaskWdtSuspendForSdMount twdt_sd;

    // CS must idle high before SPI traffic (some modules leave it floating).
    pinMode((uint8_t)SD_CS, OUTPUT);
    digitalWrite((uint8_t)SD_CS, HIGH);
    delay(1);

    // Do not call SPI.end/SD.end before first probe — cold boot + no card has caused
    // unstable behavior on some boards. SS = -1 so SD library owns CS as GPIO only.
    SPI.begin((int8_t)SD_CLK, (int8_t)SD_MISO, (int8_t)SD_MOSI, -1);
    yield();

    // 400 kHz init: fail fast on empty/floating lines. Mount at /sd (Arduino default).
    // max_files: avoid running out of FatFs file objects if other code holds descriptors.
    if (!SD.begin((uint8_t)SD_CS, SPI, 400000, "/sd", 16, false)) {
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

    if (path && path[0] != 0) {
        strncpy(path_, path, sizeof(path_) - 1);
        path_[sizeof(path_) - 1] = 0;
        if (!shortNameLooks83(path_)) {
            Serial.printf(
                "[SD] WARN: path \"%s\" is not valid 8.3 — fopen will fail (sdkconfig: FATFS_LFN_NONE).\r\n",
                path_);
        }
    } else {
        // NOT /p%08lX.log: the 'p' makes the base name 9 chars (p + 8 hex). Use 8 hex only.
        const uint32_t tag = (uint32_t)esp_random() ^ (uint32_t)millis();
        snprintf(path_, sizeof(path_), "/%08lX.log", (unsigned long)tag);
    }
    logOpenedOnce_ = false;

    ok_ = true;
    Serial.printf("[SD] logging to SD root file: %s  (on device: /sd%s)\r\n", path_, path_);
    Serial.printf("[SD] card size ~%llu MB; vol ~%llu KB free of ~%llu KB (8.3 names only, no LFN).\r\n",
                  (unsigned long long)(SD.cardSize() / (1024ULL * 1024ULL)),
                  (unsigned long long)(SD.totalBytes() > SD.usedBytes()
                                           ? (SD.totalBytes() - SD.usedBytes()) / 1024ULL
                                           : 0),
                  (unsigned long long)(SD.totalBytes() / 1024ULL));
    log("sd: logger ready");
    if (!ok_) {
        Serial.println(
            "[SD] First line could not be written — card may be read-only, full, or not FAT32. "
            "Check MOSI/MISO/CLK/CS in pins.h match your wiring (PCB vs devkit differ on MOSI).");
        SPI.end();
        SD.end();
        giveUp_ = true;
        return false;
    }
    if (!SD.exists(path_)) {
        Serial.println("[SD] WARN: exists() false right after write — try reseating the card.");
    }
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
    if (path_[0] == 0) return;
    if (xSemaphoreTake(mu_, pdMS_TO_TICKS(2000)) != pdTRUE) {
        static bool once;
        if (!once) {
            once = true;
            Serial.println("[SD] log: mutex timeout — line dropped (another caller held the lock too long).");
        }
        return;
    }

    if (SD.cardType() == CARD_NONE) {
        ok_ = false;
        Serial.println("[SD] Card removed — file logging stopped.");
        xSemaphoreGive(mu_);
        return;
    }

    // ESP32 VFS+FatFs: fopen(path, "a") often fails for a non-existent file even though mount works.
    // Create with FILE_WRITE ("w") once, then append.
    File f;
    if (!logOpenedOnce_) {
        f = SD.exists(path_) ? SD.open(path_, FILE_APPEND) : SD.open(path_, FILE_WRITE);
        if (f) {
            logOpenedOnce_ = true;
        }
    } else {
        f = SD.open(path_, FILE_APPEND);
    }
    if (f) {
        f.print(millis());
        f.print(" ms ");
        f.println(line);
        // Push through libc + VFS buffers and fsync so a sudden power cut keeps prior lines
        // (see VFSFileImpl::flush — fflush + fsync). fclose alone is not always enough in practice.
        f.flush();
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

void SdLogger::serialPrintln(const char *line) {
    if (!line) return;
    Serial.println(line);
    SdLogger::instance().log(line);
}

void SdLogger::serialPrintf(const char *fmt, ...) {
    if (!fmt) return;
    char buf[240];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    Serial.print(buf);
    SdLogger::instance().log(buf);
}

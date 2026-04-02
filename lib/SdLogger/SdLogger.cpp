#include "SdLogger.h"

#include "pins.h"
#include <FS.h>
#include <SD.h>
#include <SPI.h>
#include <stdio.h>
#include <string.h>

SdLogger &SdLogger::instance() {
    static SdLogger inst;
    return inst;
}

bool SdLogger::begin(const char *path) {
    if (mu_ == nullptr) {
        mu_ = xSemaphoreCreateMutex();
        if (!mu_) return false;
    }

    strncpy(path_, path ? path : "/paddle.log", sizeof(path_) - 1);
    path_[sizeof(path_) - 1] = 0;

    SPI.begin(SD_CLK, SD_MISO, SD_MOSI, SD_CS);
    if (!SD.begin(SD_CS, SPI, 25000000)) {
        ok_ = false;
        return false;
    }

    ok_ = true;
    log("sd: logger ready");
    return true;
}

void SdLogger::shutdown() {
    if (xSemaphoreTake(mu_, pdMS_TO_TICKS(2000)) == pdTRUE) {
        ok_ = false;
        SD.end();
        xSemaphoreGive(mu_);
    }
}

void SdLogger::log(const char *line) {
    if (!ok_ || !line) return;
    if (xSemaphoreTake(mu_, pdMS_TO_TICKS(500)) != pdTRUE) return;

    File f = SD.open(path_, FILE_APPEND);
    if (f) {
        f.print(millis());
        f.print(" ms ");
        f.println(line);
        f.close();
    } else {
        ok_ = false;
    }
    xSemaphoreGive(mu_);
}

void SdLogger::logf(const char *fmt, ...) {
    char buf[240];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    log(buf);
}

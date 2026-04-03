#pragma once

#include <Arduino.h>
#include <stdarg.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// Append-only file log on SD (SPI). Safe to call from any task (mutex inside).
class SdLogger {
public:
    static SdLogger &instance();

    bool begin(const char *path = "/paddle.log");
    void shutdown();

    bool ok() const { return ok_; }

    void log(const char *line);
    void logf(const char *fmt, ...) __attribute__((format(printf, 2, 3)));

private:
    SdLogger() = default;

    bool ok_{false};
    /** After one failed mount, never touch SD/SPI again this boot (avoids hangs/panics). */
    bool giveUp_{false};
    SemaphoreHandle_t mu_{nullptr};
    char path_[32]{"/paddle.log"};
};

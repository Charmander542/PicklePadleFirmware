#pragma once

#include <Arduino.h>
#include <stdarg.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// Append-only file log on SD (SPI). Safe to call from any task (mutex inside).
class SdLogger {
public:
    static SdLogger &instance();

    // If path is nullptr or empty, creates a strict 8.3 name each boot (FatFs LFN is off in sdkconfig).
    bool begin(const char *path = nullptr);
    void shutdown();

    bool ok() const { return ok_; }
    const char *path() const { return path_; }

    void log(const char *line);
    void logf(const char *fmt, ...) __attribute__((format(printf, 2, 3)));
    static void serialPrintln(const char *line);
    static void serialPrintf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

private:
    SdLogger() = default;

    bool ok_{false};
    /** After first successful open for this path; FatFs VFS often rejects fopen("a") on new files. */
    bool logOpenedOnce_{false};
    /** After one failed mount, never touch SD/SPI again this boot (avoids hangs/panics). */
    bool giveUp_{false};
    SemaphoreHandle_t mu_{nullptr};
    char path_[32]{};
};

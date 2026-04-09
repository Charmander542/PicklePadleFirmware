#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

enum class RunMode : uint8_t { Idle = 0, Gameplay = 1, Tutorial = 2 };

enum class UiEvent : uint8_t {
    SwingHitHost,
    ModeIdle,
    ModeGameplay,
    ModeTutorial,
};

struct UiEventMsg {
    UiEvent kind;
};

// Outgoing text messages queued by any task, drained by the network task.
struct NetOutgoingMsg {
    char text[96];
};

// FreeRTOS handles wired in main.cpp.
extern QueueHandle_t     g_uiEventQueue;  // net task → app task
extern QueueHandle_t     g_netTxQueue;    // app task → net task
extern SemaphoreHandle_t g_stateMutex;

extern RunMode g_runMode;

void    setRunMode(RunMode m);
RunMode getRunMode();

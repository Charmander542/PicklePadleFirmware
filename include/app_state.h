#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <IPAddress.h>

enum class RunMode : uint8_t { Idle = 0, Gameplay = 1, Tutorial = 2 };

enum class UiEvent : uint8_t {
  SwingHitHost,
  ModeIdle,
  ModeGameplay,
  ModeTutorial,
  SetIdleColor,
};

struct UiEventMsg {
  UiEvent kind;
  uint8_t r{0};
  uint8_t g{0};
  uint8_t b{0};
};

struct NetOutgoingMsg {
  char text[160];
};

// Globals wired in main — network task pushes UiEventMsg; app task consumes.
extern QueueHandle_t g_uiEventQueue;
extern QueueHandle_t g_netTxQueue;
extern SemaphoreHandle_t g_stateMutex;
/** Incremented when UDP RX cannot enqueue a UiEvent (queue full); check in dev builds. */
extern volatile uint32_t g_netUiEnqueueDrops;

extern RunMode g_runMode;

extern IPAddress g_hostAddr;
extern uint16_t g_hostPort;

bool hostAddrLoadFromPrefs();
void hostAddrSaveToPrefs(const IPAddress &ip, uint16_t port);

void setRunMode(RunMode m);
RunMode getRunMode();

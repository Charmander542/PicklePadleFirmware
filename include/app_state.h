#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <IPAddress.h>

enum class RunMode : uint8_t { Idle = 0, Gameplay = 1 };

enum class UiEvent : uint8_t {
  SwingHitHost,
  ModeIdle,
  ModeGameplay,
};

struct UiEventMsg {
  UiEvent kind;
};

struct NetOutgoingMsg {
  char text[96];
};

// Globals wired in main — network task pushes UiEventMsg; app task consumes.
extern QueueHandle_t g_uiEventQueue;
extern QueueHandle_t g_netTxQueue;
extern SemaphoreHandle_t g_stateMutex;

extern RunMode g_runMode;

extern IPAddress g_hostAddr;
extern uint16_t g_hostPort;

bool hostAddrLoadFromPrefs();
void hostAddrSaveToPrefs(const IPAddress &ip, uint16_t port);

void setRunMode(RunMode m);
RunMode getRunMode();

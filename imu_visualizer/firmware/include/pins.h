#pragma once

// Classic ESP32 Dev Module: SDA=21, SCL=22 (matches include/pins.h in main paddle project).
// Your PCB uses 26/27 — switch if wiring to the board, not the bare dev module.
#define IMU_SDA GPIO_NUM_26
#define IMU_SCL GPIO_NUM_27

// BNO055: 0x28 (SA0 low) or 0x29 (SA0 high). Main paddle firmware uses 0x29.
#define BNO055_ADDR 0x29

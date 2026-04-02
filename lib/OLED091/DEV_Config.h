/******************************************************************************
 * ESP32 + Arduino Wire adaptation (I2C-only, no SPI control pins).
 ******************************************************************************/
#ifndef _DEV_CONFIG_H_
#define _DEV_CONFIG_H_

#include <Arduino.h>
#include <Wire.h>

#define UBYTE uint8_t
#define UWORD uint16_t
#define UDOUBLE uint32_t

#define USE_SPI_4W 0
#define USE_IIC 1

#define IIC_CMD 0X00
#define IIC_RAM 0X40

#ifndef OLED_I2C_ADDR
#define OLED_I2C_ADDR 0x3C
#endif

uint8_t System_Init(void);
void I2C_Write_Byte(uint8_t value, uint8_t Cmd);
void Driver_Delay_ms(unsigned long xms);
void Driver_Delay_us(int xus);

#endif

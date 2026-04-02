#include "DEV_Config.h"

uint8_t System_Init(void) {
    // Wire.begin() + clock are configured by the application before OLED init.
    return 0;
}

void I2C_Write_Byte(uint8_t value, uint8_t Cmd) {
    Wire.beginTransmission((uint8_t)OLED_I2C_ADDR);
    Wire.write(Cmd);
    Wire.write(value);
    Wire.endTransmission();
}

void Driver_Delay_ms(unsigned long xms) { delay(xms); }

void Driver_Delay_us(int xus) { delayMicroseconds(xus > 0 ? xus : 1); }

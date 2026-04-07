#include "DisplayManager.h"

#include "i2c_bus_lock.h"
#include "DEV_Config.h"
#include "OLED_Driver.h"
#include "GUI_Paint.h"
#include "fonts.h"
#include "SdLogger.h"
#include <Wire.h>
#include <stdlib.h>
#include <string.h>

void DisplayManager::runInitSelfTest_() {
    /* Hardware test: 0xA5 lights all pixels without using framebuffer (SSD1306 only). */
    OLED_0in91_RefreshPanelPower();
    OLED_0in91_SetAllPixelsForcedOn(1);
    delay(350);
    OLED_0in91_SetAllPixelsForcedOn(0);
    delay(80);
    OLED_0in91_RefreshPanelPower();

    Paint_SelectImage(canvas_);
    Paint_Clear(WHITE);
    OLED_0in91_Display(canvas_);
    delay(220);
    OLED_0in91_SetInvert(1);
    delay(130);
    OLED_0in91_SetInvert(0);
    delay(90);
    Paint_Clear(BLACK);
    Paint_DrawString_EN(0, 0, "PicklePaddle", &Font12, WHITE, BLACK);
    Paint_DrawString_EN(0, 16, "Display OK", &Font12, WHITE, BLACK);
    OLED_0in91_Display(canvas_);
    delay(350);
}

bool DisplayManager::begin() {
    I2cBusLock lk;
    // OLED shares the main I2C bus with the mux and IMU — not on a mux downstream port.
    mux_.disableMuxBranches();
    delay(2);

    auto i2cAck = [](uint8_t addr) {
        Wire.beginTransmission(addr);
        return Wire.endTransmission() == 0;
    };

    uint8_t oledAddr = 0;
    if (i2cAck(OLED_I2C_ADDR)) {
        oledAddr = OLED_I2C_ADDR;
    } else if (i2cAck(OLED_I2C_ADDR_ALT)) {
        oledAddr = OLED_I2C_ADDR_ALT;
    } else {
        SdLogger::serialPrintf(
            "[OLED] No I2C ACK at 0x%02X / 0x%02X on main bus (SDA/SCL wiring / addr).\r\n",
            (unsigned)OLED_I2C_ADDR, (unsigned)OLED_I2C_ADDR_ALT);
        mux_.disableMuxBranches();
        return false;
    }
    DEV_SetOledI2CAddr(oledAddr);
    SdLogger::serialPrintf("[OLED] Using I2C 0x%02X (128x32 SSD1306, main bus)\r\n", (unsigned)oledAddr);

    System_Init();

    const UWORD imagesize =
        ((OLED_0in91_WIDTH % 8 == 0) ? (OLED_0in91_WIDTH / 8) : (OLED_0in91_WIDTH / 8 + 1)) *
        OLED_0in91_HEIGHT;

    if (!canvas_) {
        canvas_ = (uint8_t *)malloc(imagesize);
        if (!canvas_) {
            mux_.disableMuxBranches();
            return false;
        }
    }

    OLED_0in91_Init();
    Driver_Delay_ms(100);
    OLED_0in91_Clear();
    OLED_0in91_RefreshPanelPower();

    Paint_NewImage(canvas_, OLED_0in91_HEIGHT, OLED_0in91_WIDTH, 90, BLACK);
    Paint_SelectImage(canvas_);
    runInitSelfTest_();

    ok_ = true;
    mux_.disableMuxBranches();
    return true;
}

void DisplayManager::clear() {
    if (!ok_) return;
    I2cBusLock lk;
    mux_.disableMuxBranches();
    delay(1);
    Paint_SelectImage(canvas_);
    Paint_Clear(BLACK);
    OLED_0in91_Display(canvas_);
    mux_.disableMuxBranches();
}

void DisplayManager::setLine(uint8_t line, const char *text) {
    if (!ok_) return;
    I2cBusLock lk;
    mux_.disableMuxBranches();
    delay(1);
    Paint_SelectImage(canvas_);
    const UWORD y = (UWORD)(line * 12);
    Paint_DrawString_EN(0, y, text, &Font12, WHITE, BLACK);
    OLED_0in91_Display(canvas_);
    mux_.disableMuxBranches();
}

void DisplayManager::showTwoLines(const char *title, const char *subtitle) {
    if (!ok_) return;
    I2cBusLock lk;
    mux_.disableMuxBranches();
    delay(1);
    Paint_SelectImage(canvas_);
    Paint_Clear(BLACK);
    Paint_DrawString_EN(0, 0, title, &Font12, WHITE, BLACK);
    Paint_DrawString_EN(0, 16, subtitle, &Font12, WHITE, BLACK);
    OLED_0in91_Display(canvas_);
    mux_.disableMuxBranches();
}

void DisplayManager::refresh() {
    if (!ok_) return;
    I2cBusLock lk;
    mux_.disableMuxBranches();
    delay(1);
    OLED_0in91_Display(canvas_);
    mux_.disableMuxBranches();
}

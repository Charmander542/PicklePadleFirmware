#include "DisplayManager.h"

#include "DEV_Config.h"
#include "OLED_Driver.h"
#include "GUI_Paint.h"
#include "fonts.h"
#include <stdlib.h>
#include <string.h>

bool DisplayManager::begin() {
    mux_.selectChannel(TCA_CH_DISPLAY);

    System_Init();

    const UWORD imagesize =
        ((OLED_0in91_WIDTH % 8 == 0) ? (OLED_0in91_WIDTH / 8) : (OLED_0in91_WIDTH / 8 + 1)) *
        OLED_0in91_HEIGHT;

    if (!canvas_) {
        canvas_ = (uint8_t *)malloc(imagesize);
        if (!canvas_) {
            mux_.selectChannel(0);
            return false;
        }
    }

    OLED_0in91_Init();
    Driver_Delay_ms(100);
    OLED_0in91_Clear();

    Paint_NewImage(canvas_, OLED_0in91_HEIGHT, OLED_0in91_WIDTH, 90, BLACK);
    Paint_SelectImage(canvas_);
    Paint_Clear(BLACK);
    OLED_0in91_Display(canvas_);

    ok_ = true;
    mux_.selectChannel(0);
    return true;
}

void DisplayManager::clear() {
    if (!ok_) return;
    mux_.selectChannel(TCA_CH_DISPLAY);
    Paint_SelectImage(canvas_);
    Paint_Clear(BLACK);
    OLED_0in91_Display(canvas_);
    mux_.selectChannel(0);
}

void DisplayManager::setLine(uint8_t line, const char *text) {
    if (!ok_) return;
    mux_.selectChannel(TCA_CH_DISPLAY);
    Paint_SelectImage(canvas_);
    const UWORD y = (UWORD)(line * 12);
    Paint_DrawString_EN(0, y, text, &Font12, WHITE, BLACK);
    OLED_0in91_Display(canvas_);
    mux_.selectChannel(0);
}

void DisplayManager::showTwoLines(const char *title, const char *subtitle) {
    if (!ok_) return;
    mux_.selectChannel(TCA_CH_DISPLAY);
    Paint_SelectImage(canvas_);
    Paint_Clear(BLACK);
    Paint_DrawString_EN(0, 0, title, &Font12, WHITE, BLACK);
    Paint_DrawString_EN(0, 16, subtitle, &Font12, WHITE, BLACK);
    OLED_0in91_Display(canvas_);
    mux_.selectChannel(0);
}

void DisplayManager::refresh() {
    if (!ok_) return;
    mux_.selectChannel(TCA_CH_DISPLAY);
    OLED_0in91_Display(canvas_);
    mux_.selectChannel(0);
}

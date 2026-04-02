#include "fonts.h"

static const CH_CN kCnPlaceholder PROGMEM = {
    {0, 0, 0},
    {0},
};

cFONT Font12CN = {&kCnPlaceholder, 0, 12, 12, 12};
cFONT Font24CN = {&kCnPlaceholder, 0, 24, 24, 24};

#ifndef __CARDS_STYLE_COMMON_H__
#define __CARDS_STYLE_COMMON_H__

#include <lvgl.h>

namespace CardStyle {
    // Darken a color by ~45% — used for gradient stops
    lv_color_t darken(lv_color_t c);

    // Perceived luminance 0-255 (> 128 = light background)
    uint8_t luminance(lv_color_t c);

    // Returns black or white for maximum contrast on the given background
    lv_color_t contrastText(lv_color_t bg);
}

#endif // __CARDS_STYLE_COMMON_H__

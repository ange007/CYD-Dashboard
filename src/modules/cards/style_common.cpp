#include "style_common.h"

lv_color_t CardStyle::darken(lv_color_t c)
{
    return lv_color_mix(c, lv_color_black(), 140); // ~55% of original
}

uint8_t CardStyle::luminance(lv_color_t c)
{
    return lv_color32_luminance(lv_color_to_32(c, LV_OPA_COVER));
}

lv_color_t CardStyle::contrastText(lv_color_t bg)
{
    return luminance(bg) > 128 ? lv_color_black() : lv_color_white();
}

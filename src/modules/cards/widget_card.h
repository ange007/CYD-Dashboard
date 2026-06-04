#ifndef __CARDS_WIDGET_CARD_H__
#define __CARDS_WIDGET_CARD_H__

#include <lvgl.h>
#include <cJSON.h>

namespace Cards {

class WidgetCard {
public:
    // Apply bg color + auto-contrast text to all label children, optional gradient
    static void applyColor(lv_obj_t* widget, uint32_t rgb, bool isGradient);

    // Mark as scene navigator (3px blue border)
    static void applySceneStyle(lv_obj_t* widget);

    // Apply bg opacity from global settings + per-item JSON override
    static void applyBgOpacity(lv_obj_t* widget, cJSON* item);

    // Full color pipeline: reads bg_color/bg_style from JSON, applies color + opacity.
    // Uses defaultRgb when bg_color is absent.
    static void applyItemColor(lv_obj_t* widget, cJSON* item, uint32_t defaultRgb = 0xeaf0fb);
};

}

#endif // __CARDS_WIDGET_CARD_H__

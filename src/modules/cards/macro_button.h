#ifndef __CARDS_MACRO_BUTTON_H__
#define __CARDS_MACRO_BUTTON_H__

#include <lvgl.h>
#include <cJSON.h>

namespace Cards {

class MacroButton {
public:
    // Create a fully styled macro button from JSON config.
    // Applies: radius, shadow suppression, icon/image, color, opacity, border, icon size/color.
    static lv_obj_t* create(lv_obj_t* parent, cJSON* item, int btnSize);

    // Apply bg color + gradient + auto-contrast text
    static void applyColor(lv_obj_t* btn, uint32_t rgb);

    // Apply toggle on_color/off_color based on current state
    static void applyToggleColor(lv_obj_t* btn, cJSON* json, bool state);

    // Mark as scene navigator (3px themed border)
    static void applySceneStyle(lv_obj_t* btn);

    // lv_async_call callback — restore shadows after first frame.
    // Only applies when shadow is enabled AND radius == 0 (rounded shadow = LVGL OOM).
    static void restoreShadows(void* container);

    // Create button + optional title-below wrapper based on settings.
    // Adds the resulting cell to parent and returns it.
    // *outBtn receives the actual button (may differ from cell when title is below).
    static lv_obj_t* createCell(lv_obj_t* parent, cJSON* item, int btnSize, lv_obj_t** outBtn);
};

}

#endif // __CARDS_MACRO_BUTTON_H__

#include "widget_card.h"
#include "style_common.h"

#include "../../utils/settings.h"
#include "../../utils/json.h"
#include "../../ui/ui.h"

using namespace Cards;

// ── applyColor ───────────────────────────────────────────────────────────────

void WidgetCard::applyColor(lv_obj_t* widget, uint32_t rgb, bool isGradient)
{
    lv_color_t main = lv_color_hex(rgb);
    lv_color_t text = CardStyle::contrastText(main);

    lv_obj_set_style_bg_color(widget, main, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(widget, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);

    if (isGradient) {
        lv_obj_set_style_bg_grad_color(widget, CardStyle::darken(main), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_grad_dir(widget, LV_GRAD_DIR_VER, LV_PART_MAIN | LV_STATE_DEFAULT);
    } else {
        lv_obj_set_style_bg_grad_dir(widget, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    }

    // Apply text color to all direct label children (title, content)
    uint32_t n = lv_obj_get_child_cnt(widget);
    for (uint32_t i = 0; i < n; i++) {
        lv_obj_t* child = lv_obj_get_child(widget, i);
        if (child && lv_obj_check_type(child, &lv_label_class)) {
            lv_obj_set_style_text_color(child, text, LV_PART_MAIN | LV_STATE_DEFAULT);
        }
    }
}

// ── applySceneStyle ──────────────────────────────────────────────────────────

void WidgetCard::applySceneStyle(lv_obj_t* widget)
{
    lv_obj_set_style_border_width(widget, 3, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(widget, lv_color_hex(0x3d7ed4), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(widget, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
}

// ── applyBgOpacity ───────────────────────────────────────────────────────────

void WidgetCard::applyBgOpacity(lv_obj_t* widget, cJSON* item)
{
    int opa = uSettings::getWidgetBgOpa();
    cJSON* oItem = cJSON_GetObjectItem(item, "bg_opa");
    if (cJSON_IsNumber(oItem)) opa = (int)oItem->valuedouble;
    lv_obj_set_style_bg_opa(widget, (lv_opa_t)opa, LV_PART_MAIN | LV_STATE_DEFAULT);
}

// ── applyItemColor ───────────────────────────────────────────────────────────

void WidgetCard::applyItemColor(lv_obj_t* widget, cJSON* item, uint32_t defaultRgb)
{
    cJSON* bgColorItem = cJSON_GetObjectItem(item, "bg_color");
    if (bgColorItem && cJSON_IsNumber(bgColorItem)) {
        const char* bgStyle = uJSON::getString(item, "bg_style", "gradient");
        bool isGradient = !bgStyle || strcmp(bgStyle, "gradient") == 0;
        applyColor(widget, (uint32_t)bgColorItem->valueint, isGradient);
    }
    // else: no explicit bg_color in config → keep the themed widgetBg that
    // ui_comp_widget_create() already wired via ui_object_set_themeable_
    // style_property (_ui_theme_color_widgetBg). This lets the bg follow
    // the active theme (Light / Dark / Ocean / Warm / Real Black) instead
    // of being pinned to a single default RGB. The defaultRgb argument is
    // no longer used in this branch but kept in the signature for
    // backward-compatibility with callers that still pass an explicit hint.
    (void)defaultRgb;
    applyBgOpacity(widget, item);
}

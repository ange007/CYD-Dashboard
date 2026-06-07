#include "macro_button.h"
#include "style_common.h"
#include "macros.h"

#include "../hid/keyboard.h"

#include "../../utils/settings.h"
#include "../../utils/json.h"
#include "../../utils/ui.h"
#include "../../utils/sd.h"
#include "../../utils/icons.h"
#include "../../utils/media_fs.h"
#include "../../ui/ui.h"
#include "../../constants.h"

using namespace Cards;

// ── applyColor ───────────────────────────────────────────────────────────────

void MacroButton::applyColor(lv_obj_t* btn, uint32_t rgb)
{
    lv_color_t main = lv_color_hex(rgb);

    lv_obj_set_style_bg_color(btn, main, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_color(btn, CardStyle::darken(main), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t* label = lv_obj_get_child(btn, 0);
    if (label) {
        lv_obj_set_style_text_color(label, CardStyle::contrastText(main), LV_PART_MAIN | LV_STATE_DEFAULT);
    }
}

// ── applyToggleColor ─────────────────────────────────────────────────────────

void MacroButton::applyToggleColor(lv_obj_t* btn, cJSON* json, bool state)
{
    const char* colorKey = state ? "on_color" : "off_color";
    cJSON* colorItem = cJSON_GetObjectItem(json, colorKey);
    if (colorItem && cJSON_IsNumber(colorItem)) {
        applyColor(btn, (uint32_t)colorItem->valueint);
    }
}

// ── applySceneStyle ──────────────────────────────────────────────────────────

void MacroButton::applySceneStyle(lv_obj_t* btn)
{
    lv_obj_set_style_border_width(btn, 3, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(btn,
        lv_color_hex((uint32_t)ui_get_theme_value(_ui_theme_color_button)),
        LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(btn, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
}

// ── restoreShadows ───────────────────────────────────────────────────────────
// Async callback (lv_async_call): restores shadow on every macro button after
// the first rendered frame.  Runs inside lv_timer_handler().
//
// Shadow is only applied when radius == 0.  With radius > 0 LVGL allocates a
// blur mask of (btn_size + 2*shadow_width)^2 * 4 bytes per button (~24 KB for a
// 70px button) — exhausting the 48 KB LVGL heap for 2+ buttons.

void MacroButton::restoreShadows(void* userdata)
{
    if (!uSettings::getMacroShadow()) return;
    if (uSettings::getMacroRadius() != 0) return;

    lv_obj_t* container = (lv_obj_t*)userdata;
    if (!container || !lv_obj_is_valid(container)) return;

    lv_coord_t shadowW = 6;

    uint32_t cnt = lv_obj_get_child_count(container);
    for (uint32_t i = 0; i < cnt; i++) {
        lv_obj_t* child = lv_obj_get_child(container, i);
        if (!child) continue;

        // When titlePos==1 the button is wrapped in a transparent cell
        lv_obj_t* btn = child;
        if (!lv_obj_check_type(child, &lv_button_class)) {
            lv_obj_t* first = lv_obj_get_child(child, 0);
            if (first && lv_obj_check_type(first, &lv_button_class)) btn = first;
            else continue;
        }

        lv_obj_set_style_shadow_width(btn,  shadowW, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_shadow_spread(btn,       1, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_shadow_ofs_x(btn,        1, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_shadow_ofs_y(btn,        1, LV_PART_MAIN | LV_STATE_DEFAULT);
    }
}

// ── create ───────────────────────────────────────────────────────────────────
// Creates a styled macro button from JSON config item.

lv_obj_t* MacroButton::create(lv_obj_t* parent, cJSON* item, int btnSize)
{
    lv_obj_t* btn = ui_actionButton_create(parent);
    lv_obj_set_width(btn, btnSize);
    lv_obj_set_height(btn, btnSize);

    // ── Corner radius ────────────────────────────────────────────────────────
    int radius = uSettings::getMacroRadius();
    cJSON* rItem = cJSON_GetObjectItem(item, "radius");
    if (cJSON_IsNumber(rItem)) radius = (int)rItem->valuedouble;
    if (radius == 255) radius = btnSize / 2;
    lv_obj_set_style_radius(btn, radius, LV_PART_MAIN | LV_STATE_DEFAULT);

    // Suppress shadow during initial layout — restored via restoreShadows() async
    lv_obj_set_style_shadow_width(btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    // ── Content: image / icon / title ────────────────────────────────────────
    bool hasIcon = false;
    int titlePos = uSettings::getMacroTitlePos();
    const char* title = uJSON::getString(item, "title", "");

    const char* imageFile = uJSON::getString(item, "image");
    if (imageFile && imageFile[0] != '\0') {
        lv_obj_t* lbl = ui_comp_get_child(btn, UI_COMP_ACTIONBUTTON_ACTIONBUTTONLABEL);
        if (lbl) lv_label_set_text(lbl, "");

        char lvPrefixBuf[12];
        snprintf(lvPrefixBuf, sizeof(lvPrefixBuf), "%c:/icons/", uMediaFS::lvLetter());
        const char* lvPrefix = lvPrefixBuf;

        // Determine per-macro image size: s/m/l, falls back to global default.
        cJSON* imgSzItem = cJSON_GetObjectItem(item, "image_size");
        char imgSzChar = 'm';
        if (imgSzItem && cJSON_IsString(imgSzItem) && imgSzItem->valuestring && imgSzItem->valuestring[0]) {
            imgSzChar = imgSzItem->valuestring[0];
        } else {
            int defSz = uSettings::getDefaultMacroImageSize();
            if (defSz == 0) imgSzChar = 's';
            else if (defSz == 2) imgSzChar = 'l';
        }

        // Build per-size filename: "play.bin" → "play_m.bin"
        // Strip trailing ".bin" (4 chars), append "_<sz>.bin".
        size_t nameLen = strlen(imageFile);
        size_t baseLen = (nameLen > 4 && strcmp(imageFile + nameLen - 4, ".bin") == 0)
                         ? nameLen - 4 : nameLen;
        // prefix + baseName + '_' + sz + ".bin" + '\0' = prefixLen + baseLen + 6 + 1
        size_t pathLen = strlen(lvPrefix) + baseLen + 7;
        char* lvPath = (char*)malloc(pathLen);
        if (lvPath) {
            snprintf(lvPath, pathLen, "%s%.*s_%c.bin", lvPrefix, (int)baseLen, imageFile, imgSzChar);
            // Consult in-memory icon cache instead of SD.exists() — a raw
            // SD probe here races with HTTP task SD reads on the shared SPI
            // bus and periodically returns false for existing files, which
            // caused the button to fall back to its text title.
            // Cache key = basename (everything after "S:/icons/" or "L:/icons/").
            const char* basename = lvPath + strlen(lvPrefix);
            if (!uIconManager::cacheContains(basename)) {
                ESP_LOGW("MacroBtn", "image not in cache '%s', falling back to title", lvPath);
                free(lvPath);
                if (lbl) lv_label_set_text(lbl, title[0] ? title : "-");
            } else {
                ESP_LOGI("MacroBtn", "loading image '%s'", lvPath);
                // The browser pre-sizes each display variant to the exact target
                // pixel dimensions — no transform needed on device (no RAM_LOAD).
                lv_obj_t* img = lv_image_create(btn);
                lv_image_set_src(img, lvPath);
                // lv_btn uses flex layout — FLOATING removes the image from the
                // flex flow so lv_obj_align centres it over the full button area
                // instead of being wrapped to a second row and clipped.
                lv_obj_add_flag(img, LV_OBJ_FLAG_FLOATING);
                lv_obj_align(img, LV_ALIGN_CENTER, 0, 0);

                lv_obj_add_event_cb(img, [](lv_event_t *e) {
                    void* p = lv_obj_get_user_data(lv_event_get_target_obj(e));
                    if (p) free(p);
                }, LV_EVENT_DELETE, nullptr);

                lv_obj_set_user_data(img, lvPath);
            }
        }
    } else if (cJSON_HasObjectItem(item, "icon")) {
        const char* iconStr    = uJSON::getString(item, "icon");
        const char* iconSymbol = uUI::getSymbolCode(iconStr);
        lv_label_set_text(
            ui_comp_get_child(btn, UI_COMP_ACTIONBUTTON_ACTIONBUTTONLABEL),
            iconSymbol
        );
        hasIcon = true;
    } else if (titlePos == 0) {
        lv_label_set_text(
            ui_comp_get_child(btn, UI_COMP_ACTIONBUTTON_ACTIONBUTTONLABEL),
            title[0] ? title : "-"
        );
    } else {
        lv_obj_t* lbl = ui_comp_get_child(btn, UI_COMP_ACTIONBUTTON_ACTIONBUTTONLABEL);
        if (lbl) lv_label_set_text(lbl, "");
    }

    // ── Color ────────────────────────────────────────────────────────────────
    const char* itemType = uJSON::getString(item, "type");
    if (itemType && strcmp(itemType, MACROS_TYPE_TOGGLE) == 0) {
        const char* itemId = uJSON::getString(item, "id");
        bool state = Macros::getToggleState(itemId);
        const char* colorKey = state ? "on_color" : "off_color";
        cJSON* colorItem = cJSON_GetObjectItem(item, colorKey);
        if (colorItem && cJSON_IsNumber(colorItem)) {
            applyColor(btn, (uint32_t)colorItem->valueint);
        }
    } else {
        cJSON* bgColorItem = cJSON_GetObjectItem(item, "bg_color");
        if (bgColorItem && cJSON_IsNumber(bgColorItem)) {
            applyColor(btn, (uint32_t)bgColorItem->valueint);
        }
    }

    // ── Background opacity ───────────────────────────────────────────────────
    int opa = uSettings::getMacroBgOpa();
    cJSON* oItem = cJSON_GetObjectItem(item, "bg_opa");
    if (cJSON_IsNumber(oItem)) opa = (int)oItem->valuedouble;
    lv_obj_set_style_bg_opa(btn, (lv_opa_t)opa, LV_PART_MAIN | LV_STATE_DEFAULT);

    // ── Border color ─────────────────────────────────────────────────────────
    int borderClr = uSettings::getMacroBorderColor();
    cJSON* bcItem = cJSON_GetObjectItem(item, "border_color");
    if (cJSON_IsNumber(bcItem)) borderClr = (int)bcItem->valuedouble;
    if (borderClr != 0) {
        lv_obj_set_style_border_width(btn, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_color(btn, lv_color_hex((uint32_t)borderClr), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_opa(btn, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    } else {
        lv_obj_set_style_border_width(btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    }

    // ── Icon font size ───────────────────────────────────────────────────────
    lv_obj_t* label = ui_comp_get_child(btn, UI_COMP_ACTIONBUTTON_ACTIONBUTTONLABEL);
    if (hasIcon && label) {
        // Per-macro icon_size takes priority; fall back to global default (0=s,1=m,2=l).
        cJSON* szItem = cJSON_GetObjectItem(item, "icon_size");
        char szChar = 's';
        if (szItem && cJSON_IsString(szItem) && szItem->valuestring && szItem->valuestring[0]) {
            szChar = szItem->valuestring[0];
        } else {
            int defSz = uSettings::getDefaultMacroIconSize();
            if (defSz == 1) szChar = 'm';
            else if (defSz == 2) szChar = 'l';
        }
        const lv_font_t* font = &lv_font_montserrat_14;
        if (szChar == 'm') font = &lv_font_montserrat_22;
        if (szChar == 'l') font = &lv_font_montserrat_32;
        lv_obj_set_style_text_font(label, font, LV_PART_MAIN | LV_STATE_DEFAULT);
    }

    // ── Icon color override ──────────────────────────────────────────────────
    // Per-macro icon_color takes priority; fall back to global default (0 = auto/contrast).
    if (hasIcon && label) {
        cJSON* iconColorItem = cJSON_GetObjectItem(item, "icon_color");
        int iconClr = (iconColorItem && cJSON_IsNumber(iconColorItem))
                      ? iconColorItem->valueint
                      : uSettings::getDefaultMacroIconColor();
        if (iconClr != 0) {
            lv_obj_set_style_text_color(label,
                lv_color_hex((uint32_t)iconClr),
                LV_PART_MAIN | LV_STATE_DEFAULT);
        }
        // iconClr == 0: leave the contrast colour already set by applyColor()
    }

    // ── User data (freed on LV_EVENT_DELETE) ─────────────────────────────────
    uUI::attachJsonData(btn, item);

    return btn;
}

// ── needsHid ─────────────────────────────────────────────────────────────────

bool MacroButton::needsHid(cJSON* item) {
    const char* type = uJSON::getString(item, "type");
    if (!type) return false;
    if (strcmp(type, MACROS_TYPE_KEYS) == 0) return true;
    if (strcmp(type, MACROS_TYPE_MULTI) == 0) {
        cJSON* actions = cJSON_GetObjectItem(item, "actions");
        if (!cJSON_IsArray(actions)) return false;
        cJSON* act = NULL;
        cJSON_ArrayForEach(act, actions) {
            const char* atype = uJSON::getString(act, "type");
            if (atype && (strcmp(atype, "keys") == 0 || strcmp(atype, "text") == 0))
                return true;
        }
        return false;
    }
    return false;
}

// ── createCell ───────────────────────────────────────────────────────────────
// When titlePos == 1 and item has a non-empty title: creates a transparent
// flex-column wrapper containing the button + a title label below it.
// Otherwise returns the button itself as the cell.

lv_obj_t* MacroButton::createCell(lv_obj_t* parent, cJSON* item, int btnSize, lv_obj_t** outBtn)
{
    int titlePos = uSettings::getMacroTitlePos();
    const char* title = uJSON::getString(item, "title", "");
    bool useTitleBelow = (titlePos == 1 && title && title[0] != '\0');

    lv_obj_t* btnParent = parent;
    lv_obj_t* cell      = nullptr;

    if (useTitleBelow) {
        cell = lv_obj_create(parent);
        lv_obj_set_width(cell, btnSize);
        lv_obj_set_height(cell, LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(cell, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_width(cell, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_pad_all(cell, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_shadow_width(cell, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_flex_flow(cell, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(cell, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_clear_flag(cell, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_pad_row(cell, 3, LV_PART_MAIN | LV_STATE_DEFAULT);
        btnParent = cell;
    }

    lv_obj_t* btn = create(btnParent, item, btnSize);
    if (outBtn) *outBtn = btn;

    if (needsHid(item) && !HidKeyboard::isConnected())
        lv_obj_add_state(btn, LV_STATE_DISABLED);

    if (!useTitleBelow) return btn;  // cell IS the button

    lv_obj_t* titleLbl = lv_label_create(cell);
    lv_obj_set_width(titleLbl, lv_pct(100));
    lv_obj_set_height(titleLbl, LV_SIZE_CONTENT);
    lv_label_set_text(titleLbl, title);
    lv_label_set_long_mode(titleLbl, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(titleLbl, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(titleLbl, &lv_font_montserrat_14, LV_PART_MAIN | LV_STATE_DEFAULT);
    ui_object_set_themeable_style_property(titleLbl, LV_PART_MAIN | LV_STATE_DEFAULT, LV_STYLE_TEXT_COLOR, _ui_theme_color_text);
    ui_object_set_themeable_style_property(titleLbl, LV_PART_MAIN | LV_STATE_DEFAULT, LV_STYLE_TEXT_OPA, _ui_theme_alpha_text);

    return cell;
}

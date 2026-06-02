#include "../ui.h"

void ui_screenMain_screen_init(void)
{
    ui_screenMain = lv_obj_create(NULL);
    lv_obj_clear_flag(ui_screenMain, LV_OBJ_FLAG_SCROLLABLE);      /// Flags
    lv_obj_set_flex_flow(ui_screenMain, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(ui_screenMain, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_SPACE_BETWEEN);
    ui_object_set_themeable_style_property(ui_screenMain, LV_PART_MAIN | LV_STATE_DEFAULT, LV_STYLE_BG_COLOR, _ui_theme_color_background);
    ui_object_set_themeable_style_property(ui_screenMain, LV_PART_MAIN | LV_STATE_DEFAULT, LV_STYLE_BG_OPA, _ui_theme_alpha_background);

    ui_tabsMain = lv_tabview_create(ui_screenMain);
    lv_tabview_set_tab_bar_position(ui_tabsMain, LV_DIR_TOP);
    lv_tabview_set_tab_bar_size(ui_tabsMain, 35);
    lv_obj_set_width(ui_tabsMain, lv_pct(100));
    lv_obj_set_height(ui_tabsMain, lv_pct(100));
    lv_obj_set_x(ui_tabsMain, 0);
    lv_obj_set_y(ui_tabsMain, 0);
    lv_obj_clear_flag(ui_tabsMain, LV_OBJ_FLAG_SCROLLABLE);      /// Flags
    lv_obj_set_style_pad_left(ui_tabsMain, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(ui_tabsMain, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(ui_tabsMain, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(ui_tabsMain, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    // In LVGL 9 the tab bar is a plain flex-container (lv_obj), NOT a btnmatrix.
    // LV_PART_ITEMS has no effect on a flex-container — individual tab button styles
    // (DEFAULT + CHECKED per-button) are applied in applyTabPosition() after all
    // tabs have been added so the button children already exist.
    lv_obj_t* tab_bar = lv_tabview_get_tab_bar(ui_tabsMain);
    ui_object_set_themeable_style_property(tab_bar, LV_PART_MAIN | LV_STATE_DEFAULT, LV_STYLE_BG_COLOR, _ui_theme_color_panelBackground);
    ui_object_set_themeable_style_property(tab_bar, LV_PART_MAIN | LV_STATE_DEFAULT, LV_STYLE_BG_OPA, _ui_theme_alpha_panelBackground);
    // Bottom border separates the tab bar from the tab content area. In
    // Real Black theme (where panel bg == screen bg == 0x000000) this is
    // the ONLY visual separator; in other themes the bg color contrast
    // already implies the divide but a 1 px border makes it crisp.
    ui_object_set_themeable_style_property(tab_bar, LV_PART_MAIN | LV_STATE_DEFAULT, LV_STYLE_BORDER_COLOR, _ui_theme_color_widgetBorder);
    ui_object_set_themeable_style_property(tab_bar, LV_PART_MAIN | LV_STATE_DEFAULT, LV_STYLE_BORDER_OPA, _ui_theme_alpha_widgetBorder);
    lv_obj_set_style_border_width(tab_bar, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(tab_bar, LV_BORDER_SIDE_BOTTOM, LV_PART_MAIN | LV_STATE_DEFAULT);

    // Zero padding + border on tabview content container so each tab's BG
    // reaches all four edges (default theme leaves a few-pixel inset which
    // reveals the parent screen BG around the tab).
    lv_obj_t* tab_content = lv_tabview_get_content(ui_tabsMain);
    lv_obj_set_style_pad_all(tab_content, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(tab_content, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    //
    ui_macrosTab_init(ui_tabsMain);
    ui_widgetsTab_init(ui_tabsMain);

    //
    ui_bottomPanel_init(ui_screenMain);
}

void ui_macrosTab_init(lv_obj_t *comp_parent)
{
    ui_tabMacros = lv_tabview_add_tab(comp_parent, LV_SYMBOL_KEYBOARD);
    ui_object_set_themeable_style_property(ui_tabMacros, LV_PART_MAIN | LV_STATE_DEFAULT, LV_STYLE_BG_COLOR, _ui_theme_color_background);
    ui_object_set_themeable_style_property(ui_tabMacros, LV_PART_MAIN | LV_STATE_DEFAULT, LV_STYLE_BG_OPA, _ui_theme_alpha_background);
    // Tab itself has zero padding — its bg image (set at runtime) stretches
    // to all four edges.  The 10 px inset previously here is moved to the
    // inner flex container (ui_cntMacros) so child items still have margin.
    lv_obj_set_style_pad_all(ui_tabMacros, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_cntMacros = lv_obj_create(ui_tabMacros);
    lv_obj_remove_style_all(ui_cntMacros);
    lv_obj_set_width(ui_cntMacros, lv_pct(100));
    lv_obj_set_height(ui_cntMacros, lv_pct(100));
    lv_obj_set_align(ui_cntMacros, LV_ALIGN_CENTER);
    lv_obj_set_flex_flow(ui_cntMacros, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(ui_cntMacros, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(ui_cntMacros, LV_OBJ_FLAG_CLICKABLE);      /// Flags
    lv_obj_set_style_pad_left(ui_cntMacros, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(ui_cntMacros, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(ui_cntMacros, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(ui_cntMacros, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(ui_cntMacros, 5, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(ui_cntMacros, 5, LV_PART_MAIN | LV_STATE_DEFAULT);
}

void ui_widgetsTab_init(lv_obj_t *comp_parent)
{
    ui_tabWidget = lv_tabview_add_tab(comp_parent, LV_SYMBOL_LIST);
    lv_obj_set_flex_flow(ui_tabWidget, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(ui_tabWidget, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    ui_object_set_themeable_style_property(ui_tabWidget, LV_PART_MAIN | LV_STATE_DEFAULT, LV_STYLE_BG_COLOR, _ui_theme_color_background);
    ui_object_set_themeable_style_property(ui_tabWidget, LV_PART_MAIN | LV_STATE_DEFAULT, LV_STYLE_BG_OPA, _ui_theme_alpha_background);
    // Same rationale as ui_tabMacros: zero pad on the tab, move inset to cnt.
    lv_obj_set_style_pad_all(ui_tabWidget, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(ui_tabWidget, 5, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(ui_tabWidget, 5, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_cntWidgets = lv_obj_create(ui_tabWidget);
    lv_obj_remove_style_all(ui_cntWidgets);
    lv_obj_set_width(ui_cntWidgets, lv_pct(100));
    lv_obj_set_height(ui_cntWidgets, lv_pct(100));
    lv_obj_set_align(ui_cntWidgets, LV_ALIGN_CENTER);
    lv_obj_set_flex_flow(ui_cntWidgets, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(ui_cntWidgets, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(ui_cntWidgets, LV_OBJ_FLAG_CLICKABLE);      /// Flags
    lv_obj_set_style_pad_left(ui_cntWidgets, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(ui_cntWidgets, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(ui_cntWidgets, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(ui_cntWidgets, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(ui_cntWidgets, 5, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(ui_cntWidgets, 5, LV_PART_MAIN | LV_STATE_DEFAULT);
}

void ui_bottomPanel_init(lv_obj_t * comp_parent)
{
    // ── Status bar: 3-column flex row [Left | Center(grow) | Right] ──────────
    ui_pnlStatus = lv_obj_create(comp_parent);
    lv_obj_set_height(ui_pnlStatus, 45);
    lv_obj_set_width(ui_pnlStatus, lv_pct(100));
    lv_obj_set_align(ui_pnlStatus, LV_ALIGN_BOTTOM_MID);
    lv_obj_set_flex_flow(ui_pnlStatus, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(ui_pnlStatus, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_flag(ui_pnlStatus, LV_OBJ_FLAG_FLOATING | LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    lv_obj_clear_flag(ui_pnlStatus, LV_OBJ_FLAG_SCROLLABLE);
    ui_object_set_themeable_style_property(ui_pnlStatus, LV_PART_MAIN | LV_STATE_DEFAULT, LV_STYLE_BG_COLOR, _ui_theme_color_panelBackground);
    ui_object_set_themeable_style_property(ui_pnlStatus, LV_PART_MAIN | LV_STATE_DEFAULT, LV_STYLE_BG_OPA,   _ui_theme_alpha_panelBackground);
    // Top border separates status panel from tab content above. Essential in
    // Real Black (same bg colour everywhere); cosmetically clean in others.
    ui_object_set_themeable_style_property(ui_pnlStatus, LV_PART_MAIN | LV_STATE_DEFAULT, LV_STYLE_BORDER_COLOR, _ui_theme_color_widgetBorder);
    ui_object_set_themeable_style_property(ui_pnlStatus, LV_PART_MAIN | LV_STATE_DEFAULT, LV_STYLE_BORDER_OPA,   _ui_theme_alpha_widgetBorder);
    lv_obj_set_style_border_width(ui_pnlStatus, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(ui_pnlStatus, LV_BORDER_SIDE_TOP, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(ui_pnlStatus, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(ui_pnlStatus, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(ui_pnlStatus, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(ui_pnlStatus, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(ui_pnlStatus, 5, LV_PART_MAIN | LV_STATE_DEFAULT);

    // ── Left column: WiFi + Settings ─────────────────────────────────────────
    ui_pnlStatusLeft = lv_obj_create(ui_pnlStatus);
    lv_obj_remove_style_all(ui_pnlStatusLeft);
    lv_obj_set_height(ui_pnlStatusLeft, lv_pct(100));
    lv_obj_set_width(ui_pnlStatusLeft, LV_SIZE_CONTENT);
    lv_obj_clear_flag(ui_pnlStatusLeft, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(ui_pnlStatusLeft, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(ui_pnlStatusLeft, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    ui_object_set_themeable_style_property(ui_pnlStatusLeft, LV_PART_MAIN | LV_STATE_DEFAULT, LV_STYLE_BG_COLOR, _ui_theme_color_panelBackground);
    ui_object_set_themeable_style_property(ui_pnlStatusLeft, LV_PART_MAIN | LV_STATE_DEFAULT, LV_STYLE_BG_OPA,   _ui_theme_alpha_panelBackground);
    lv_obj_set_style_pad_all(ui_pnlStatusLeft, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(ui_pnlStatusLeft, 5, LV_PART_MAIN | LV_STATE_DEFAULT);

    // ── Center column: Profile (grows to fill available space) ───────────────
    ui_cntProfile = lv_obj_create(ui_pnlStatus);
    lv_obj_remove_style_all(ui_cntProfile);
    lv_obj_set_height(ui_cntProfile, lv_pct(100));
    lv_obj_set_flex_grow(ui_cntProfile, 1);
    lv_obj_set_flex_flow(ui_cntProfile, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(ui_cntProfile, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(ui_cntProfile, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    // ── Right column: Bluetooth + Companion ───────────────────────────────────
    ui_pnlStatusRight = lv_obj_create(ui_pnlStatus);
    lv_obj_remove_style_all(ui_pnlStatusRight);
    lv_obj_set_height(ui_pnlStatusRight, lv_pct(100));
    lv_obj_set_width(ui_pnlStatusRight, LV_SIZE_CONTENT);
    lv_obj_clear_flag(ui_pnlStatusRight, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(ui_pnlStatusRight, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(ui_pnlStatusRight, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    ui_object_set_themeable_style_property(ui_pnlStatusRight, LV_PART_MAIN | LV_STATE_DEFAULT, LV_STYLE_BG_COLOR, _ui_theme_color_panelBackground);
    ui_object_set_themeable_style_property(ui_pnlStatusRight, LV_PART_MAIN | LV_STATE_DEFAULT, LV_STYLE_BG_OPA,   _ui_theme_alpha_panelBackground);
    lv_obj_set_style_pad_all(ui_pnlStatusRight, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(ui_pnlStatusRight, 5, LV_PART_MAIN | LV_STATE_DEFAULT);

    // ── Profile label + button (inside center column) ─────────────────────────
    ui_lblProfile = lv_label_create(ui_cntProfile);
    lv_obj_set_width(ui_lblProfile, LV_SIZE_CONTENT);
    lv_obj_set_height(ui_lblProfile, LV_SIZE_CONTENT);
    lv_label_set_text(ui_lblProfile, "Profile:");
    ui_object_set_themeable_style_property(ui_lblProfile, LV_PART_MAIN | LV_STATE_DEFAULT, LV_STYLE_TEXT_COLOR, _ui_theme_color_text);
    ui_object_set_themeable_style_property(ui_lblProfile, LV_PART_MAIN | LV_STATE_DEFAULT, LV_STYLE_TEXT_OPA,   _ui_theme_alpha_text);
    lv_obj_set_style_pad_right(ui_lblProfile, 4, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_ddProfile = lv_btn_create(ui_cntProfile);
    lv_obj_set_flex_grow(ui_ddProfile, 1);      // fill remaining center width
    lv_obj_set_height(ui_ddProfile, 30);
    lv_obj_clear_flag(ui_ddProfile, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(ui_ddProfile, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(ui_ddProfile, 5, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(ui_ddProfile, 5, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(ui_ddProfile, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(ui_ddProfile, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
    ui_object_set_themeable_style_property(ui_ddProfile, LV_PART_MAIN | LV_STATE_DEFAULT, LV_STYLE_BG_COLOR, _ui_theme_color_button);
    ui_object_set_themeable_style_property(ui_ddProfile, LV_PART_MAIN | LV_STATE_DEFAULT, LV_STYLE_BG_OPA,   _ui_theme_alpha_button);

    lv_obj_t * ui_lblProfileVal = lv_label_create(ui_ddProfile);
    lv_label_set_text(ui_lblProfileVal, "All");
    lv_label_set_long_mode(ui_lblProfileVal, LV_LABEL_LONG_DOT);
    lv_obj_set_width(ui_lblProfileVal, lv_pct(100));
    lv_obj_center(ui_lblProfileVal);
    ui_object_set_themeable_style_property(ui_lblProfileVal, LV_PART_MAIN | LV_STATE_DEFAULT, LV_STYLE_TEXT_COLOR, _ui_theme_color_buttonText);
    ui_object_set_themeable_style_property(ui_lblProfileVal, LV_PART_MAIN | LV_STATE_DEFAULT, LV_STYLE_TEXT_OPA,   _ui_theme_alpha_buttonText);

    lv_obj_add_event_cb(ui_ddProfile, OnProfileBtnClicked, LV_EVENT_CLICKED, NULL);

    // ── WiFi + Settings buttons (left column) ─────────────────────────────────
    ui_btnWifi = lv_btn_create(ui_pnlStatusLeft);
    lv_obj_set_width(ui_btnWifi, 30);
    lv_obj_set_height(ui_btnWifi, 30);
    lv_obj_add_flag(ui_btnWifi, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_obj_clear_flag(ui_btnWifi, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(ui_btnWifi, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
    ui_object_set_themeable_style_property(ui_btnWifi, LV_PART_MAIN | LV_STATE_DEFAULT, LV_STYLE_BG_COLOR, _ui_theme_color_button);
    ui_object_set_themeable_style_property(ui_btnWifi, LV_PART_MAIN | LV_STATE_DEFAULT, LV_STYLE_BG_OPA,   _ui_theme_alpha_button);
    lv_obj_set_style_text_font(ui_btnWifi, &lv_font_montserrat_14, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_lblWifi = lv_label_create(ui_btnWifi);
    lv_obj_set_width(ui_lblWifi, LV_SIZE_CONTENT);
    lv_obj_set_height(ui_lblWifi, LV_SIZE_CONTENT);
    lv_obj_set_align(ui_lblWifi, LV_ALIGN_CENTER);
    lv_label_set_text(ui_lblWifi, LV_SYMBOL_WIFI);
    ui_object_set_themeable_style_property(ui_lblWifi, LV_PART_MAIN | LV_STATE_DEFAULT, LV_STYLE_TEXT_COLOR, _ui_theme_color_buttonText);
    ui_object_set_themeable_style_property(ui_lblWifi, LV_PART_MAIN | LV_STATE_DEFAULT, LV_STYLE_TEXT_OPA,   _ui_theme_alpha_buttonText);

    ui_btnSetting = lv_btn_create(ui_pnlStatusLeft);
    lv_obj_set_width(ui_btnSetting, 30);
    lv_obj_set_height(ui_btnSetting, 30);
    lv_obj_add_flag(ui_btnSetting, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_obj_clear_flag(ui_btnSetting, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(ui_btnSetting, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
    ui_object_set_themeable_style_property(ui_btnSetting, LV_PART_MAIN | LV_STATE_DEFAULT, LV_STYLE_BG_COLOR, _ui_theme_color_button);
    ui_object_set_themeable_style_property(ui_btnSetting, LV_PART_MAIN | LV_STATE_DEFAULT, LV_STYLE_BG_OPA,   _ui_theme_alpha_button);

    ui_lblSetting = lv_label_create(ui_btnSetting);
    lv_obj_set_width(ui_lblSetting, LV_SIZE_CONTENT);
    lv_obj_set_height(ui_lblSetting, LV_SIZE_CONTENT);
    lv_obj_set_align(ui_lblSetting, LV_ALIGN_CENTER);
    lv_label_set_text(ui_lblSetting, LV_SYMBOL_SETTINGS);
    ui_object_set_themeable_style_property(ui_lblSetting, LV_PART_MAIN | LV_STATE_DEFAULT, LV_STYLE_TEXT_COLOR, _ui_theme_color_buttonText);
    ui_object_set_themeable_style_property(ui_lblSetting, LV_PART_MAIN | LV_STATE_DEFAULT, LV_STYLE_TEXT_OPA,   _ui_theme_alpha_buttonText);

    lv_obj_add_event_cb(ui_btnWifi, ui_event_btnWifi, LV_EVENT_ALL, NULL);
    lv_obj_add_event_cb(ui_btnSetting, ui_event_btnSetting, LV_EVENT_ALL, NULL);

    // ── Bluetooth + Companion buttons (right column) ──────────────────────────
    ui_lblBluetooth = lv_label_create(ui_pnlStatusRight);
    lv_obj_set_width(ui_lblBluetooth, LV_SIZE_CONTENT);
    lv_obj_set_height(ui_lblBluetooth, LV_SIZE_CONTENT);
    lv_label_set_text(ui_lblBluetooth, LV_SYMBOL_BLUETOOTH);
    lv_obj_set_style_text_font(ui_lblBluetooth, &lv_font_montserrat_14, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(ui_lblBluetooth, lv_color_hex(0xFF0000), LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_lblCompanion = lv_label_create(ui_pnlStatusRight);
    lv_obj_set_width(ui_lblCompanion, LV_SIZE_CONTENT);
    lv_obj_set_height(ui_lblCompanion, LV_SIZE_CONTENT);
    lv_label_set_text(ui_lblCompanion, LV_SYMBOL_CALL);
    lv_obj_set_style_text_font(ui_lblCompanion, &lv_font_montserrat_14, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(ui_lblCompanion, lv_color_hex(0xFF0000), LV_PART_MAIN | LV_STATE_DEFAULT);

    #ifdef BOARD_HAS_TF
        ui_lblSD = lv_label_create(ui_pnlStatusRight);
        lv_obj_set_width(ui_lblSD, LV_SIZE_CONTENT);
        lv_obj_set_height(ui_lblSD, LV_SIZE_CONTENT);
        lv_label_set_text(ui_lblSD, LV_SYMBOL_SD_CARD);
        lv_obj_set_style_text_font(ui_lblSD, &lv_font_montserrat_14, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_color(ui_lblSD, lv_color_hex(0xFF8C00), LV_PART_MAIN | LV_STATE_DEFAULT);
    #endif
}
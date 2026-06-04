#include "../ui.h"

// Widget card width — adapts to screen size at compile time.
// Height is LV_SIZE_CONTENT so each widget type sizes itself to its content.
// DISPLAY_WIDTH is injected by the board definition (esp32-smartdisplay).
#if defined(DISPLAY_WIDTH) && DISPLAY_WIDTH >= 800
  #define WIDGET_CARD_WIDTH  200
#elif defined(DISPLAY_WIDTH) && DISPLAY_WIDTH >= 480
  #define WIDGET_CARD_WIDTH  165
#else
  #define WIDGET_CARD_WIDTH  130
#endif

void ui_event_comp_widget(lv_event_t * e)
{
    lv_event_code_t event_code = lv_event_get_code(e);
    lv_obj_t * target = lv_event_get_target_obj(e);
    lv_obj_t ** comp_widget = lv_event_get_user_data(e);
    if(event_code == LV_EVENT_CLICKED) {
        OnWidgetClicked(e);
    } else if(event_code == LV_EVENT_LONG_PRESSED) {
        OnWidgetLongPressed(e);
    }
}

// COMPONENT widget

lv_obj_t * ui_widget_create(lv_obj_t * comp_parent)
{
    lv_obj_t * cui_widget;
    cui_widget = lv_obj_create(comp_parent);
    lv_obj_set_width(cui_widget, WIDGET_CARD_WIDTH);
    lv_obj_set_height(cui_widget, LV_SIZE_CONTENT);
    lv_obj_set_align(cui_widget, LV_ALIGN_CENTER);
    lv_obj_set_flex_flow(cui_widget, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cui_widget, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(cui_widget, LV_OBJ_FLAG_SCROLLABLE);      /// Flags
    lv_obj_set_style_radius(cui_widget, 5, LV_PART_MAIN | LV_STATE_DEFAULT);
    ui_object_set_themeable_style_property(cui_widget, LV_PART_MAIN | LV_STATE_DEFAULT, LV_STYLE_BG_COLOR, _ui_theme_color_widgetBg);
    ui_object_set_themeable_style_property(cui_widget, LV_PART_MAIN | LV_STATE_DEFAULT, LV_STYLE_BG_OPA,   _ui_theme_alpha_widgetBg);
    ui_object_set_themeable_style_property(cui_widget, LV_PART_MAIN | LV_STATE_DEFAULT, LV_STYLE_BORDER_COLOR, _ui_theme_color_widgetBorder);
    ui_object_set_themeable_style_property(cui_widget, LV_PART_MAIN | LV_STATE_DEFAULT, LV_STYLE_BORDER_OPA,   _ui_theme_alpha_widgetBorder);
    lv_obj_set_style_border_width(cui_widget, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(cui_widget, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(cui_widget, 5, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(cui_widget, 5, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(cui_widget, 5, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(cui_widget, 5, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(cui_widget, 4, LV_PART_MAIN | LV_STATE_DEFAULT);  // gap between title line and content

    // Subtle shadow for card depth effect
    lv_obj_set_style_shadow_width(cui_widget, 6, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(cui_widget, 40, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(cui_widget, lv_color_black(), LV_PART_MAIN | LV_STATE_DEFAULT);

    // Error state (LV_STATE_USER_1): thick red bottom border, no other sides
    ui_object_set_themeable_style_property(cui_widget, LV_PART_MAIN | LV_STATE_USER_1, LV_STYLE_BORDER_COLOR, _ui_theme_color_error);
    ui_object_set_themeable_style_property(cui_widget, LV_PART_MAIN | LV_STATE_USER_1, LV_STYLE_BORDER_OPA,   _ui_theme_alpha_error);
    lv_obj_set_style_border_width(cui_widget, 3, LV_PART_MAIN | LV_STATE_USER_1);
    lv_obj_set_style_border_side(cui_widget, LV_BORDER_SIDE_BOTTOM, LV_PART_MAIN | LV_STATE_USER_1);

    return cui_widget;
}

// Shared title label — accent color + bottom separator line.
// Every widget type calls this instead of duplicating the styling block.
lv_obj_t * ui_widget_add_title(lv_obj_t * parent)
{
    lv_obj_t * title = lv_label_create(parent);
    lv_obj_set_width(title, lv_pct(100));
    lv_obj_set_height(title, LV_SIZE_CONTENT);
    lv_label_set_text(title, "-");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    ui_object_set_themeable_style_property(title, LV_PART_MAIN | LV_STATE_DEFAULT, LV_STYLE_TEXT_COLOR,   _ui_theme_color_accent);
    ui_object_set_themeable_style_property(title, LV_PART_MAIN | LV_STATE_DEFAULT, LV_STYLE_TEXT_OPA,     _ui_theme_alpha_accent);
    ui_object_set_themeable_style_property(title, LV_PART_MAIN | LV_STATE_DEFAULT, LV_STYLE_BORDER_COLOR, _ui_theme_color_accent);
    ui_object_set_themeable_style_property(title, LV_PART_MAIN | LV_STATE_DEFAULT, LV_STYLE_BORDER_OPA,   _ui_theme_alpha_accent);
    lv_obj_set_style_border_width(title, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(title, LV_BORDER_SIDE_BOTTOM, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(title, 4, LV_PART_MAIN | LV_STATE_DEFAULT);
    return title;
}


#include "../ui.h"

// COMPONENT widgetProgress

static void progress_event_cb(lv_event_t * e)
{
    lv_obj_t * obj = lv_event_get_target_obj(e);
    lv_layer_t * layer = lv_event_get_layer(e);

    lv_draw_label_dsc_t label_dsc;
    lv_draw_label_dsc_init(&label_dsc);
    label_dsc.font = LV_FONT_DEFAULT;

    char buf[8];
    lv_snprintf(buf, sizeof(buf), "%d", (int)lv_bar_get_value(obj));
    label_dsc.text = buf;
    label_dsc.text_local = 1;

    lv_point_t txt_size;
    lv_text_get_size(&txt_size, buf, label_dsc.font, label_dsc.letter_space, label_dsc.line_space, LV_COORD_MAX, label_dsc.flag);

    lv_area_t bar_area;
    lv_obj_get_coords(obj, &bar_area);

    lv_area_t txt_area;

    /* If the indicator is long enough put the text inside on the right */
    int32_t indic_w = (int32_t)lv_bar_get_value(obj) * lv_area_get_width(&bar_area) / 100;
    if (indic_w > txt_size.x + 20) {
        txt_area.x2 = bar_area.x1 + indic_w - 5;
        txt_area.x1 = txt_area.x2 - txt_size.x + 1;
        label_dsc.color = lv_color_white();
    }
    /* If the indicator is still short put the text out of it on the right */
    else {
        txt_area.x1 = bar_area.x1 + indic_w + 5;
        txt_area.x2 = txt_area.x1 + txt_size.x - 1;
        label_dsc.color = lv_color_black();
    }

    txt_area.y1 = bar_area.y1 + (lv_area_get_height(&bar_area) - txt_size.y) / 2;
    txt_area.y2 = txt_area.y1 + txt_size.y - 1;

    lv_draw_label(layer, &label_dsc, &txt_area);
}

lv_obj_t * ui_widgetProgress_create(lv_obj_t * comp_parent, bool is_vertical)
{    
    lv_obj_t * cui_widget;
    cui_widget = ui_widget_create(comp_parent);
    lv_obj_set_width(cui_widget, 100);
    // Cross-axis center so a narrow vertical bar is centered horizontally
    lv_obj_set_flex_align(cui_widget, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);

    lv_obj_t * cui_title = ui_widget_add_title(cui_widget);

    // Progress — participates in flex column (no lv_obj_center, no fixed card height)
    lv_obj_t * cui_progress;
    cui_progress = lv_bar_create(cui_widget);
    lv_bar_set_range(cui_progress, 0, 100);
    lv_bar_set_value(cui_progress, 0, LV_ANIM_ON);

    if (is_vertical) { lv_obj_set_size(cui_progress, 20, 200); }    // narrow × tall
    else             { lv_obj_set_size(cui_progress, lv_pct(100), 20); } // full-width × short

    // Rounded ends on both track and indicator
    lv_obj_set_style_radius(cui_progress, 4, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(cui_progress, 4, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    // Theme-colored indicator
    ui_object_set_themeable_style_property(cui_progress, LV_PART_INDICATOR | LV_STATE_DEFAULT, LV_STYLE_BG_COLOR, _ui_theme_color_button);
    ui_object_set_themeable_style_property(cui_progress, LV_PART_INDICATOR | LV_STATE_DEFAULT, LV_STYLE_BG_OPA,   _ui_theme_alpha_button);

    lv_obj_add_event_cb(cui_progress, progress_event_cb, LV_EVENT_DRAW_MAIN_END, NULL);

    lv_obj_t ** children = lv_malloc(sizeof(lv_obj_t *) * _UI_COMP_WIDGET_PROGRESS_NUM);
    children[UI_COMP_WIDGET_PROGRESS_PANEL] = cui_widget;
    children[UI_COMP_WIDGET_PROGRESS_TITLE] = cui_title;
    children[UI_COMP_WIDGET_PROGRESS_CONTENT] = cui_progress;
    lv_obj_add_event_cb(cui_widget, get_component_child_event_cb, LV_EVENT_GET_COMP_CHILD, children);
    lv_obj_add_event_cb(cui_widget, del_component_child_event_cb, LV_EVENT_DELETE, children);
    lv_obj_add_event_cb(cui_widget, ui_event_comp_widget, LV_EVENT_ALL, children);

    return cui_widget;
}


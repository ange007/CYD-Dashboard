#include "../ui.h"

// COMPONENT widgetArc

lv_obj_t * ui_widgetArc_create(lv_obj_t * comp_parent)
{
    lv_obj_t * cui_widget;
    lv_obj_t * cui_title;
    lv_obj_t * cui_arc;
    lv_obj_t * cui_caption;

    cui_widget = ui_widget_create(comp_parent);
    lv_obj_set_width(cui_widget, 100);
    lv_obj_clear_flag(cui_widget, LV_OBJ_FLAG_SCROLLABLE);
    // Center arc horizontally in the flex column
    lv_obj_set_flex_align(cui_widget, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);

    cui_title = ui_widget_add_title(cui_widget);

    // Arc
    cui_arc = lv_arc_create(cui_widget);
    lv_obj_set_size(cui_arc, 72, 72);
    lv_arc_set_rotation(cui_arc, 135);
    lv_arc_set_bg_angles(cui_arc, 0, 270);
    lv_arc_set_range(cui_arc, 0, 100);
    lv_arc_set_value(cui_arc, 0);
    lv_obj_remove_style(cui_arc, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(cui_arc, LV_OBJ_FLAG_CLICKABLE);
    // Wider arc lines with rounded indicator ends
    lv_obj_set_style_arc_width(cui_arc, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_width(cui_arc, 8, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_rounded(cui_arc, true, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    ui_object_set_themeable_style_property(cui_arc, LV_PART_INDICATOR | LV_STATE_DEFAULT, LV_STYLE_ARC_COLOR, _ui_theme_color_button);

    // Caption
    cui_caption = lv_label_create(cui_arc);
    lv_obj_set_size(cui_caption, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_align(cui_caption, LV_ALIGN_CENTER);
    lv_label_set_text(cui_caption, "-");

    ui_object_set_themeable_style_property(cui_caption, LV_PART_MAIN | LV_STATE_DEFAULT, LV_STYLE_TEXT_COLOR, _ui_theme_color_accent);
    ui_object_set_themeable_style_property(cui_caption, LV_PART_MAIN | LV_STATE_DEFAULT, LV_STYLE_TEXT_OPA,   _ui_theme_alpha_accent);
    lv_obj_set_style_pad_all(cui_caption, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    // Structure
    lv_obj_t ** children = lv_malloc(sizeof(lv_obj_t *) * _UI_COMP_WIDGET_ARC_NUM);
    children[UI_COMP_WIDGET_ARC_PANEL] = cui_widget;
    children[UI_COMP_WIDGET_ARC_TITLE] = cui_title;
    children[UI_COMP_WIDGET_ARC_PROGRESS] = cui_arc;
    children[UI_COMP_WIDGET_ARC_CAPTION] = cui_caption;

    lv_obj_add_event_cb(cui_widget, get_component_child_event_cb, LV_EVENT_GET_COMP_CHILD, children);
    lv_obj_add_event_cb(cui_widget, del_component_child_event_cb, LV_EVENT_DELETE, children);
    lv_obj_add_event_cb(cui_widget, ui_event_comp_widget, LV_EVENT_ALL, children);

    return cui_widget;
}


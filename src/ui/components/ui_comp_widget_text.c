#include "../ui.h"

// COMPONENT widgetText

lv_obj_t * ui_widgetText_create(lv_obj_t * comp_parent)
{
    lv_obj_t * cui_widget;
    cui_widget = ui_widget_create(comp_parent);
    lv_obj_set_width(cui_widget, 145);

    lv_obj_t * cui_title = ui_widget_add_title(cui_widget);

    lv_obj_t * cui_content;
    cui_content = lv_label_create(cui_widget);
    lv_obj_set_width(cui_content, lv_pct(100));
    lv_obj_set_height(cui_content, LV_SIZE_CONTENT);
    lv_obj_set_align(cui_content, LV_ALIGN_CENTER);
    lv_label_set_long_mode(cui_content, LV_LABEL_LONG_WRAP);
    lv_label_set_text(cui_content, "-");

    lv_obj_t ** children = lv_malloc(sizeof(lv_obj_t *) * _UI_COMP_WIDGET_TEXT_NUM);
    children[UI_COMP_WIDGET_TEXT_PANEL] = cui_widget;
    children[UI_COMP_WIDGET_TEXT_TITLE] = cui_title;
    children[UI_COMP_WIDGET_TEXT_CONTENT] = cui_content;
    lv_obj_add_event_cb(cui_widget, get_component_child_event_cb, LV_EVENT_GET_COMP_CHILD, children);
    lv_obj_add_event_cb(cui_widget, del_component_child_event_cb, LV_EVENT_DELETE, children);
    lv_obj_add_event_cb(cui_widget, ui_event_comp_widget, LV_EVENT_ALL, children);
    // ui_comp_actionButton_create_hook(cui_actionButton);

    return cui_widget;
}


#include "../ui.h"

// COMPONENT widgetImage
// Displays a static PNG from the SD card.  The image source is set by the
// caller (widgets.cpp) via lv_image_set_src() on the content child.

lv_obj_t * ui_widgetImage_create(lv_obj_t * comp_parent)
{
    lv_obj_t * cui_widget = ui_widget_create(comp_parent);

    lv_obj_t * cui_title = ui_widget_add_title(cui_widget);

    // Image placeholder — source set externally after creation
    lv_obj_t * cui_image = lv_image_create(cui_widget);
    lv_obj_set_width(cui_image, lv_pct(100));
    lv_obj_set_height(cui_image, LV_SIZE_CONTENT);
    lv_image_set_inner_align(cui_image, LV_IMAGE_ALIGN_CENTER);

    lv_obj_t ** children = lv_malloc(sizeof(lv_obj_t *) * _UI_COMP_WIDGET_IMAGE_NUM);
    children[UI_COMP_WIDGET_IMAGE_PANEL]   = cui_widget;
    children[UI_COMP_WIDGET_IMAGE_TITLE]   = cui_title;
    children[UI_COMP_WIDGET_IMAGE_CONTENT] = cui_image;

    lv_obj_add_event_cb(cui_widget, get_component_child_event_cb, LV_EVENT_GET_COMP_CHILD, children);
    lv_obj_add_event_cb(cui_widget, del_component_child_event_cb, LV_EVENT_DELETE, children);
    lv_obj_add_event_cb(cui_widget, ui_event_comp_widget, LV_EVENT_ALL, children);

    return cui_widget;
}

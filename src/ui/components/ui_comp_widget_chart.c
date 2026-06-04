#include "../ui.h"

// Chart content height — fixed px so the parent (LV_SIZE_CONTENT card) sizes correctly.
// lv_pct(80) is meaningless when parent height is LV_SIZE_CONTENT.
#if defined(DISPLAY_WIDTH) && DISPLAY_WIDTH >= 800
  #define CHART_CONTENT_HEIGHT 100
#elif defined(DISPLAY_WIDTH) && DISPLAY_WIDTH >= 480
  #define CHART_CONTENT_HEIGHT 80
#else
  #define CHART_CONTENT_HEIGHT 60
#endif

// COMPONENT widgetChart

lv_obj_t * ui_widgetChart_create(lv_obj_t * comp_parent)
{
    lv_obj_t * cui_widget;
    cui_widget = ui_widget_create(comp_parent);

    lv_obj_t * cui_title = ui_widget_add_title(cui_widget);

    lv_obj_t * cui_chart;
    cui_chart = lv_chart_create(cui_widget);
    lv_obj_set_width(cui_chart, lv_pct(100));
    lv_obj_set_height(cui_chart, CHART_CONTENT_HEIGHT);
    lv_chart_set_type(cui_chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(cui_chart, 20);
    lv_obj_set_style_size(cui_chart, 0, 0, LV_PART_INDICATOR); // hide dots

    // Muted grid lines
    lv_obj_set_style_line_color(cui_chart, lv_color_hex(0x334155), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_line_opa(cui_chart, 80, LV_PART_MAIN | LV_STATE_DEFAULT);

    // Single rolling series — all points start as LV_CHART_POINT_NONE (not drawn)
    lv_chart_add_series(cui_chart,
        lv_color_hex((uint32_t)ui_get_theme_value(_ui_theme_color_button)),
        LV_CHART_AXIS_PRIMARY_Y);


    lv_obj_t ** children = lv_malloc(sizeof(lv_obj_t *) * _UI_COMP_WIDGET_CHART_NUM);
    children[UI_COMP_WIDGET_CHART_PANEL] = cui_widget;
    children[UI_COMP_WIDGET_CHART_TITLE] = cui_title;
    children[UI_COMP_WIDGET_CHART_CHART] = cui_chart;
    lv_obj_add_event_cb(cui_widget, get_component_child_event_cb, LV_EVENT_GET_COMP_CHILD, children);
    lv_obj_add_event_cb(cui_widget, del_component_child_event_cb, LV_EVENT_DELETE, children);
    lv_obj_add_event_cb(cui_widget, ui_event_comp_widget, LV_EVENT_ALL, children);

    return cui_widget;
}


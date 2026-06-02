#ifndef _UI_COMP_WIDGET_CHART_H
#define _UI_COMP_WIDGET_CHART_H

#include "../ui.h"

#ifdef __cplusplus
extern "C" {
#endif

// COMPONENT widgetChart
#define UI_COMP_WIDGET_CHART_PANEL 0
#define UI_COMP_WIDGET_CHART_TITLE 1
#define UI_COMP_WIDGET_CHART_CHART 2

#define _UI_COMP_WIDGET_CHART_NUM 3

lv_obj_t * ui_widgetChart_create(lv_obj_t * comp_parent);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif

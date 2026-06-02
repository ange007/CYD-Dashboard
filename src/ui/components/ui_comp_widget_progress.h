#ifndef _UI_COMP_WIDGET_PROGRESS_H
#define _UI_COMP_WIDGET_PROGRESS_H

#include "../ui.h"

#ifdef __cplusplus
extern "C" {
#endif

// COMPONENT widgetProgress
#define UI_COMP_WIDGET_PROGRESS_PANEL 0
#define UI_COMP_WIDGET_PROGRESS_TITLE 1
#define UI_COMP_WIDGET_PROGRESS_CONTENT 2

#define _UI_COMP_WIDGET_PROGRESS_NUM 3

lv_obj_t * ui_widgetProgress_create(lv_obj_t * comp_parent, bool is_vertical);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif // _UI_COMP_WIDGET_PROGRESS_H

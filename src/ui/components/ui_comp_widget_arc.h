#ifndef _UI_COMP_WIDGET_ARC_H
#define _UI_COMP_WIDGET_ARC_H

#include "../ui.h"

#ifdef __cplusplus
extern "C" {
#endif

// COMPONENT widgetArc
#define UI_COMP_WIDGET_ARC_PANEL 0
#define UI_COMP_WIDGET_ARC_TITLE 1
#define UI_COMP_WIDGET_ARC_PROGRESS 2
#define UI_COMP_WIDGET_ARC_CAPTION 3


#define _UI_COMP_WIDGET_ARC_NUM 4

lv_obj_t * ui_widgetArc_create(lv_obj_t * comp_parent);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif // _UI_COMP_WIDGET_ARC_H

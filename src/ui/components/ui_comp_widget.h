#ifndef _UI_COMP_WIDGET_H
#define _UI_COMP_WIDGET_H

#include "../ui.h"

#ifdef __cplusplus
extern "C" {
#endif
lv_obj_t * ui_widget_create(lv_obj_t * comp_parent);
lv_obj_t * ui_widget_add_title(lv_obj_t * parent);
void ui_event_comp_widget(lv_event_t * e);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif // _UI_COMP_WIDGET_H

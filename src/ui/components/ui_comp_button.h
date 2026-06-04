#ifndef _UI_COMP_BUTTON_H
#define _UI_COMP_BUTTON_H

#include "../ui.h"

#ifdef __cplusplus
extern "C" {
#endif

// COMPONENT BUTTON
#define UI_COMP_BUTTON_BUTTON 0
#define UI_COMP_BUTTON_LABEL 1
#define _UI_COMP_BUTTON_NUM 2
lv_obj_t * ui_button_create(lv_obj_t * comp_parent);
void ui_event_comp_button_button(lv_event_t * e);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif

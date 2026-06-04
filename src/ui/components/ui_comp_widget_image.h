// Image widget component — displays a static PNG from the SD card (/icons/ directory).
// Max recommended image size: 128×128 px.

#ifndef _UI_COMP_WIDGET_IMAGE_H
#define _UI_COMP_WIDGET_IMAGE_H

#include "../ui.h"

#ifdef __cplusplus
extern "C" {
#endif

// COMPONENT widgetImage
#define UI_COMP_WIDGET_IMAGE_PANEL   0
#define UI_COMP_WIDGET_IMAGE_TITLE   1
#define UI_COMP_WIDGET_IMAGE_CONTENT 2

#define _UI_COMP_WIDGET_IMAGE_NUM 3

lv_obj_t * ui_widgetImage_create(lv_obj_t * comp_parent);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif // _UI_COMP_WIDGET_IMAGE_H

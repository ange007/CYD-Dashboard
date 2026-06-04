#ifndef __LVFS_LITTLEFS_H__
#define __LVFS_LITTLEFS_H__

// Registers an LVGL filesystem driver with letter 'L' that wraps LittleFS.
// After calling this, lv_image_set_src() accepts paths like "L:/icons/user/myicon.png".
// Must be called once after LittleFS.begin() and before any LVGL image load.
void lvfs_littlefs_init();

#endif // __LVFS_LITTLEFS_H__

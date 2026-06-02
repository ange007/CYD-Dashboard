#include "lvfs_sd.h"

#ifdef BOARD_HAS_TF

#include <lvgl.h>
#include <SD.h>  // Arduino SD library (angle brackets avoid collision with local sd.h)

// LVGL FS driver for SD card, registered as letter 'S'.
// Files are accessed via lv_image_set_src("S:/path/to/file.png").
// Requires uSD::init() to have been called (and succeeded) before use.

static void* lvfs_sd_open(lv_fs_drv_t* /*drv*/, const char* path, lv_fs_mode_t mode)
{
    const char* flags = (mode == LV_FS_MODE_WR) ? FILE_WRITE : FILE_READ;
    ESP_LOGI("LVFS_SD", "open path='%s'", path);
    File* f = new File(SD.open(path, flags));
    if (!f || !*f) {
        ESP_LOGW("LVFS_SD", "open FAILED path='%s'", path);
        delete f;
        return nullptr;
    }
    ESP_LOGI("LVFS_SD", "open OK size=%u", (unsigned)f->size());
    return f;
}

static lv_fs_res_t lvfs_sd_close(lv_fs_drv_t* /*drv*/, void* file_p)
{
    File* f = static_cast<File*>(file_p);
    f->close();
    delete f;
    return LV_FS_RES_OK;
}

static lv_fs_res_t lvfs_sd_read(lv_fs_drv_t* /*drv*/, void* file_p, void* buf, uint32_t btr, uint32_t* br)
{
    *br = static_cast<File*>(file_p)->read(static_cast<uint8_t*>(buf), btr);
    return LV_FS_RES_OK;
}

static lv_fs_res_t lvfs_sd_seek(lv_fs_drv_t* /*drv*/, void* file_p, uint32_t pos, lv_fs_whence_t whence)
{
    SeekMode mode = SeekSet;
    if (whence == LV_FS_SEEK_CUR) mode = SeekCur;
    else if (whence == LV_FS_SEEK_END) mode = SeekEnd;
    static_cast<File*>(file_p)->seek(pos, mode);
    return LV_FS_RES_OK;
}

static lv_fs_res_t lvfs_sd_tell(lv_fs_drv_t* /*drv*/, void* file_p, uint32_t* pos)
{
    *pos = static_cast<File*>(file_p)->position();
    return LV_FS_RES_OK;
}

void lvfs_sd_init()
{
    static lv_fs_drv_t drv;
    lv_fs_drv_init(&drv);

    drv.letter   = 'S';
    drv.open_cb  = lvfs_sd_open;
    drv.close_cb = lvfs_sd_close;
    drv.read_cb  = lvfs_sd_read;
    drv.seek_cb  = lvfs_sd_seek;
    drv.tell_cb  = lvfs_sd_tell;

    lv_fs_drv_register(&drv);
}

#endif // BOARD_HAS_TF

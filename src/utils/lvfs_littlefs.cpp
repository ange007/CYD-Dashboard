#include "lvfs_littlefs.h"

#include <lvgl.h>
#include <LittleFS.h>

// Each open file is represented by a heap-allocated File object.
// LVGL passes this pointer back as `file_p` in every callback.

static void* lvfs_open(lv_fs_drv_t* /*drv*/, const char* path, lv_fs_mode_t mode)
{
    const char* flags = (mode == LV_FS_MODE_WR) ? "w" : "r";
    File* f = new File(LittleFS.open(path, flags));
    if (!f || !*f) {
        delete f;
        return nullptr;
    }
    return f;
}

static lv_fs_res_t lvfs_close(lv_fs_drv_t* /*drv*/, void* file_p)
{
    File* f = static_cast<File*>(file_p);
    f->close();
    delete f;
    return LV_FS_RES_OK;
}

static lv_fs_res_t lvfs_read(lv_fs_drv_t* /*drv*/, void* file_p, void* buf, uint32_t btr, uint32_t* br)
{
    *br = static_cast<File*>(file_p)->read(static_cast<uint8_t*>(buf), btr);
    return LV_FS_RES_OK;
}

static lv_fs_res_t lvfs_seek(lv_fs_drv_t* /*drv*/, void* file_p, uint32_t pos, lv_fs_whence_t whence)
{
    SeekMode mode = SeekSet;
    if (whence == LV_FS_SEEK_CUR) mode = SeekCur;
    else if (whence == LV_FS_SEEK_END) mode = SeekEnd;
    static_cast<File*>(file_p)->seek(pos, mode);
    return LV_FS_RES_OK;
}

static lv_fs_res_t lvfs_tell(lv_fs_drv_t* /*drv*/, void* file_p, uint32_t* pos)
{
    *pos = static_cast<File*>(file_p)->position();
    return LV_FS_RES_OK;
}

void lvfs_littlefs_init()
{
    static lv_fs_drv_t drv;
    lv_fs_drv_init(&drv);

    drv.letter   = 'L';
    drv.open_cb  = lvfs_open;
    drv.close_cb = lvfs_close;
    drv.read_cb  = lvfs_read;
    drv.seek_cb  = lvfs_seek;
    drv.tell_cb  = lvfs_tell;

    lv_fs_drv_register(&drv);
}

#ifndef __SD_H__
#define __SD_H__

#include "FS.h"

class uSD {
public:
    static bool init();
    static bool isAvailable();
    static fs::FS& getFS();

    // Set/query SD-serving flag so widget HTTP fetches pause during large file transfers
    static void setSdServing(bool serving);
    static bool isSdServing();

    static uint64_t getCardSize();

    static void createDir(fs::FS &fs, const char *path);
    static void removeDir(fs::FS &fs, const char *path);
    static void readFile(fs::FS &fs, const char *path);
    static void writeFile(fs::FS &fs, const char *path, const char *message);
    static void appendFile(fs::FS &fs, const char *path, const char *message);
    static void renameFile(fs::FS &fs, const char *path1, const char *path2);
    static void deleteFile(fs::FS &fs, const char *path);
};

#endif // __SD_H__

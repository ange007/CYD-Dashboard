#ifndef __ICONS_H__
#define __ICONS_H__

#include "Arduino.h"

// Icon file management — list, validate, delete.
// All operations work on the SD card paths /icons/ and /icons/src/.
// Upload (chunked HTTP) is handled by server_base helpers.
class uIconManager {
public:
    // Returns a JSON array string of icon filenames in /icons/ (flat, no src/).
    // e.g. ["play_m.bin","play_s.bin","play_l.bin"]
    // Returns "[]" if SD unavailable or directory empty.
    static String listJson();

    // Validate an icon filename: must not contain '/' or "..".
    // Fills errorOut on failure. Returns true if valid.
    static bool validateFilename(const String& name, String& errorOut);

    // Delete icon: removes all per-size variants (_s/_m/_l),
    // the legacy single-file (if present), and the source file in /icons/src/.
    // Requires SD available and valid filename (call validateFilename first).
    static bool deleteIcon(const String& name, String& errorOut);

    // In-memory icon cache — prevents SD.exists() races with concurrent HTTP
    // SD reads (shared SPI bus, no mutex). Populated by buildCache() at boot
    // and updated on upload/delete. Consulted by macro renderer each draw.
    static void buildCache();
    static void cacheAdd(const String& filename);
    static void cacheRemove(const String& filename);
    static bool cacheContains(const char* filename);
};

#endif // __ICONS_H__

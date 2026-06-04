#include "icons.h"
#include "sd.h"

#include <set>
#include <string>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// ── Icon cache ────────────────────────────────────────────────────────────────
// Holds the filenames present in /icons/ (flat, no src/). Avoids SD.exists()
// calls from the LVGL render path, which race with HTTP task SD reads on the
// shared SPI bus and can spuriously return false → icon falls back to title.
static std::set<std::string> _iconCache;
static SemaphoreHandle_t     _iconCacheMutex = nullptr;

static void ensureCacheMutex() {
    if (!_iconCacheMutex) _iconCacheMutex = xSemaphoreCreateMutex();
}

struct CacheLock {
    CacheLock()  { ensureCacheMutex(); xSemaphoreTake(_iconCacheMutex, portMAX_DELAY); }
    ~CacheLock() { xSemaphoreGive(_iconCacheMutex); }
};

void uIconManager::buildCache() {
    CacheLock _l;
    _iconCache.clear();
    File dir = uSD::getFS().open("/icons");
    if (!dir || !dir.isDirectory()) return;
    File f = dir.openNextFile();
    while (f) {
        if (!f.isDirectory()) {
            const char *n  = f.name();
            const char *sl = strrchr(n, '/');
            _iconCache.insert(sl ? sl + 1 : n);
        }
        f = dir.openNextFile();
    }
}

void uIconManager::cacheAdd(const String& filename) {
    CacheLock _l;
    _iconCache.insert(std::string(filename.c_str()));
}

void uIconManager::cacheRemove(const String& filename) {
    CacheLock _l;
    _iconCache.erase(std::string(filename.c_str()));
}

bool uIconManager::cacheContains(const char* filename) {
    if (!filename || !*filename) return false;
    CacheLock _l;
    return _iconCache.find(filename) != _iconCache.end();
}

String uIconManager::listJson() {
    String out = "[";
    out.reserve(256);
    File dir = uSD::getFS().open("/icons");
    if (dir && dir.isDirectory()) {
        bool first = true;
        File f = dir.openNextFile();
        while (f) {
            if (!f.isDirectory()) {
                if (!first) out += ',';
                out += '"';
                const char *n = f.name();
                const char *sl = strrchr(n, '/');
                out += (sl ? sl + 1 : n);
                out += '"';
                first = false;
            }
            f = dir.openNextFile();
        }
    }
    out += ']';
    return out;
}

bool uIconManager::validateFilename(const String& name, String& errorOut) {
    if (name.indexOf('/') >= 0 || name.indexOf("..") >= 0) {
        errorOut = "invalid name";
        return false;
    }
    return true;
}

bool uIconManager::deleteIcon(const String& name, String& errorOut) {
    if (!uSD::isAvailable()) { errorOut = "no SD card"; return false; }
    if (!validateFilename(name, errorOut)) return false;

    // Delete all per-size variants (_s/_m/_l).
    String base = name;
    if (base.endsWith(".bin")) base = base.substring(0, base.length() - 4);
    for (const char* sz : {"s", "m", "l"}) {
        char p[128];
        snprintf(p, sizeof(p), "/icons/%s_%s.bin", base.c_str(), sz);
        if (uSD::getFS().exists(p)) uSD::getFS().remove(p);
        char shortName[96];
        snprintf(shortName, sizeof(shortName), "%s_%s.bin", base.c_str(), sz);
        cacheRemove(shortName);
    }

    // Legacy: single-file icon without size suffix (@deprecated).
    char singleIcon[128];
    snprintf(singleIcon, sizeof(singleIcon), "/icons/%s", name.c_str());
    if (uSD::getFS().exists(singleIcon)) uSD::getFS().remove(singleIcon);
    cacheRemove(name);

    // Source file — try the legacy .bin location plus all compressed variants.
    // SPA references sources as <base>.bin but the actual stored file may
    // be .png/.jpg/.svg/.webp/.gif after the switch to keep originals.
    char srcPath[128];
    snprintf(srcPath, sizeof(srcPath), "/icons/src/%s", name.c_str());
    if (uSD::getFS().exists(srcPath)) uSD::getFS().remove(srcPath);
    const char* exts[] = { ".png", ".jpg", ".jpeg", ".svg", ".webp", ".gif" };
    for (const char* ext : exts) {
        char altPath[128];
        snprintf(altPath, sizeof(altPath), "/icons/src/%s%s", base.c_str(), ext);
        if (uSD::getFS().exists(altPath)) uSD::getFS().remove(altPath);
    }

    return true;
}

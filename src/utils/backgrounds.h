#ifndef __BACKGROUNDS_H__
#define __BACKGROUNDS_H__

#include "Arduino.h"

// Background file and scene-assignment management.
// Files are stored in /backgrounds/ on the SD card.
// Scene-to-background mapping is persisted in NVS under key "scene_bgs" (JSON object).
class uBackgroundManager {
public:
    // Returns a JSON array of {name, size} objects for all files in /backgrounds/.
    // e.g. [{"name":"home.jpg","size":12345}]
    // Returns "[]" if SD unavailable or directory empty.
    static String listJson();

    // Validate a background filename: must not contain '/' or "..".
    static bool validateFilename(const String& name, String& errorOut);

    // Delete a background file from /backgrounds/.
    static bool deleteBackground(const String& name, String& errorOut);

    // Get the full scene-background JSON map from NVS.
    // Returns "{}" if not set.
    static String getSceneBgsJson();

    // Assign (or clear) a background for a scene. Pass image="" to remove.
    // Writes to NVS only. Caller must set _pending_scene_bg_reload so that
    // uSettings::reloadSceneBgs() runs on the main task (not from HTTP task).
    static bool setSceneBg(const String& sceneId, const String& image, String& errorOut);
};

#endif // __BACKGROUNDS_H__

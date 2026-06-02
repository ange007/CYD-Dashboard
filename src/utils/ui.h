#ifndef __UI_H__
#define __UI_H__

#include <WString.h>
#include <map>
#include <cJSON.h>
#include "lvgl.h"

class uUI {
public:
    static std::map<String, const char*> getSymbolsDictionary();
    static const char* getSymbolCode(const char *key);
    static lv_obj_t* getObjectById(lv_obj_t *parent, const char *id);
    static cJSON* getObjectDataJSON(lv_obj_t *object);

    // Serialize json → string, attach as user_data, register LV_EVENT_DELETE to free it.
    // Does NOT take ownership of json — caller must still cJSON_Delete(json).
    static void attachJsonData(lv_obj_t* obj, cJSON* json);
};

#endif // __UI_H__
#ifndef __CARDS_PROFILES_H__
#define __CARDS_PROFILES_H__

#include <cJSON.h>
#include <string>

#define MEMORY_PROFILES_KEY "profiles"

namespace Cards {
    class Profiles {
    public:
        static void init();                     // load from NVS, populate dropdown
        static void apply(const char* profileId); // navigate both tabs + save active
        static cJSON* getList();                // returns allocated cJSON (caller must delete)
        static void   set(cJSON* list);         // save to NVS + rebuild dropdown
        static std::string getActiveId();
        static void setActive(const char* id);  // save only (no apply)
    };
}

#endif // __CARDS_PROFILES_H__

#ifndef __MEMORY_H__
#define __MEMORY_H__

#include <Preferences.h>

class uMemory {
    typedef std::function<void(Preferences preferences)> MemoryCallback;
public:
    static void write(const char *ns, MemoryCallback callback);
    static void read(const char *ns, MemoryCallback callback);
    static void remove(const char *ns, const char *key);
    static void clear(const char *ns);
    static void clearAll();
};

#endif // __MEMORY_H__
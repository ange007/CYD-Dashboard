#ifndef __M_TIMERS_H__
#define __M_TIMERS_H__

#include "ReactESP.h"

#include <functional>
#include <vector>

class mTimers {
public:
    using TimerCallback = std::function<void(reactesp::RepeatReaction*, std::string, std::string)>;

private:
    static reactesp::ReactESP app;

    struct TaskCallbackData {
        TimerCallback callback;
        reactesp::RepeatReaction* task;
        std::string objectType;
        std::string objectId;
    };

    static std::vector<TaskCallbackData> callbackDataList;
public:
    static bool add(long interval, const char* objectType, const char* objectId, TimerCallback callback);
    // Same as add() but the first tick is delayed by `firstDelayMs` (jitter).
    // Use to spread N timers with identical intervals across their first cycle,
    // so they don't all fire simultaneously at boot (WS / HTTP burst).
    static bool addJittered(long firstDelayMs, long interval, const char* objectType,
                            const char* objectId, TimerCallback callback);
    static bool remove(const char* objectType, const char* objectId);
    static void clear();
    static void clearByType(const char* objectType);

    static void loop();
private:
    static reactesp::RepeatReaction* createTask(long interval, const char* objectType, const char* objectId, TimerCallback callback);
};

#endif // __M_TIMERS_H__

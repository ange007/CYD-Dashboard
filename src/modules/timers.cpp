#include "timers.h"

#include <Arduino.h>

reactesp::ReactESP mTimers::app;
std::vector<mTimers::TaskCallbackData> mTimers::callbackDataList;

bool mTimers::add(long interval, const char* objectType, const char* objectId, TimerCallback callback)
{
    reactesp::RepeatReaction* task = createTask(interval, objectType, objectId, callback);

    return task != nullptr;
}

bool mTimers::addJittered(long firstDelayMs, long interval, const char* objectType,
                           const char* objectId, TimerCallback callback)
{
    if (firstDelayMs <= 0) {
        return add(interval, objectType, objectId, callback);
    }

    // Delay first fire by firstDelayMs, then install the normal repeat timer.
    // Capture by value so the lambda is independent of caller scope.
    std::string ot(objectType ? objectType : "");
    std::string oi(objectId   ? objectId   : "");
    TimerCallback cb = callback;

    app.onDelay(firstDelayMs, [interval, ot, oi, cb]() {
        createTask(interval, ot.c_str(), oi.c_str(), cb);
        // Fire once immediately so user sees data without waiting a full cycle.
        cb(nullptr, ot, oi);
    });
    return true;
}

bool mTimers::remove(const char* objectType, const char* objectId)
{
    for (auto it = callbackDataList.begin(); it != callbackDataList.end(); ++it) {
        if (it->task != nullptr && it->objectType == objectType && it->objectId == objectId) {
            // app.remove() sets enabled=false; ReactESP's tickTimed() deletes
            // the object when it is popped from timed_queue. Do NOT delete here
            // — that would free memory still referenced by the priority queue,
            // causing a use-after-free crash in the next ReactESP::tick() call.
            app.remove(it->task);
            callbackDataList.erase(it);
            return true;
        }
    }

    return false;
}

void mTimers::clear()
{
    for (auto& cd : callbackDataList) {
        if (cd.task != nullptr) {
            app.remove(cd.task);  // marks disabled; ReactESP deletes on next tick pop
        }
    }
    callbackDataList.clear();
}

void mTimers::clearByType(const char* objectType)
{
    for (auto it = callbackDataList.begin(); it != callbackDataList.end(); ) {
        if (it->task != nullptr && it->objectType == objectType) {
            app.remove(it->task);  // marks disabled; ReactESP deletes on next tick pop
            it = callbackDataList.erase(it);
        } else {
            ++it;
        }
    }
}

void mTimers::loop() {
    app.tick();
}

reactesp::RepeatReaction* mTimers::createTask(long interval, const char* objectType, const char* objectId, TimerCallback callback)
{
    reactesp::RepeatReaction* task = nullptr;

    try {
        // Push entry first so the vector slot exists before the lambda fires.
        // The task pointer is patched in after app.onRepeat() returns.
        // NOTE: the RepeatReaction* argument passed to the callback is always
        // nullptr — callers must not rely on it.  Use mTimers::remove() by
        // objectType/objectId instead.
        callbackDataList.push_back({ callback, nullptr, std::string(objectType), std::string(objectId) });

        // Capture strings by value so the lambda is self-contained and safe
        // even if callbackDataList reallocates after push_back.
        std::string ot(objectType);
        std::string oi(objectId);
        task = app.onRepeat(interval, [callback, ot, oi]() {
            callback(nullptr, ot, oi);
        });

        callbackDataList.back().task = task;
    } catch (std::bad_alloc& e) {
        if (task) delete task;
        if (!callbackDataList.empty()) callbackDataList.pop_back();
        return nullptr;
    }

    return task;
}
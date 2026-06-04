#pragma once

#include <Arduino.h>
#include <esp_log.h>
#include <esp_heap_caps.h>

// RAII heap delta logger. Construct at the top of a scope to capture
// free heap + largest free block; destruct at scope exit logs both deltas iff
// either is nonzero. Debug-only: release builds compile to an empty class with
// zero overhead.
//
// Usage:
//   void save() {
//       HeapProbe probe("settings.save");
//       // ... work ...
//   }   // logs "heap[settings.save] tot=-16 largest=-512 (before tot=… lf=…)"
//
// Also exposes one-shot HeapProbe::snapshot(label) for non-scoped points.

#if CORE_DEBUG_LEVEL >= ARDUHAL_LOG_LEVEL_DEBUG
class HeapProbe {
    const char* _label;
    uint32_t    _beforeTotal;
    uint32_t    _beforeLargest;
public:
    explicit HeapProbe(const char* label)
        : _label(label),
          _beforeTotal(ESP.getFreeHeap()),
          _beforeLargest(heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT)) {}

    ~HeapProbe() {
        uint32_t afterTotal   = ESP.getFreeHeap();
        uint32_t afterLargest = heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT);
        int32_t  dt = (int32_t)afterTotal   - (int32_t)_beforeTotal;
        int32_t  dl = (int32_t)afterLargest - (int32_t)_beforeLargest;
        if (dt != 0 || dl != 0) {
            log_i("heap[%s] tot=%+d largest=%+d (before tot=%u lf=%u, after tot=%u lf=%u)",
                  _label, (int)dt, (int)dl,
                  (unsigned)_beforeTotal,  (unsigned)_beforeLargest,
                  (unsigned)afterTotal,    (unsigned)afterLargest);
        }
    }

    // Print current heap state without recording a delta. Use at cycle
    // boundaries where there is no natural scope to wrap.
    static void snapshot(const char* label) {
        uint32_t tot = ESP.getFreeHeap();
        uint32_t lf  = heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT);
        log_i("heap-snap[%s] tot=%u lf=%u", label, (unsigned)tot, (unsigned)lf);
    }

    HeapProbe(const HeapProbe&)            = delete;
    HeapProbe& operator=(const HeapProbe&) = delete;
};
#else
class HeapProbe {
public:
    explicit HeapProbe(const char*) {}
    static void snapshot(const char*) {}
};
#endif

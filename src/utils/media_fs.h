#pragma once
#include "sd.h"

namespace uMediaFS {
    // Runtime LVGL drive letter — 'S' if SD active, 'L' otherwise.
    inline char lvLetter() { return uSD::isAvailable() ? 'S' : 'L'; }
}

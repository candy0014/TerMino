#ifndef TETR_UTIL_TIMER_RESOLUTION_H
#define TETR_UTIL_TIMER_RESOLUTION_H

#ifdef _WIN32
#include <windows.h>
#endif

namespace Util {
// Windows otherwise rounds short waits to a roughly 16ms system tick. Keep
// game cadence and the relay's staggered image deliveries at millisecond scale.
inline void enable_precise_waits() {
#ifdef _WIN32
    struct Period {
        using Change = UINT (WINAPI*)(UINT);
        HMODULE library = LoadLibraryExW(L"winmm.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        Change end = nullptr;
        Period() {
            if (!library) return;
            const auto begin = reinterpret_cast<Change>(GetProcAddress(library, "timeBeginPeriod"));
            const auto finish = reinterpret_cast<Change>(GetProcAddress(library, "timeEndPeriod"));
            if (begin && finish && begin(1) == 0) end = finish;
        }
        ~Period() {
            if (end) end(1);
            if (library) FreeLibrary(library);
        }
    };
    static const Period period;
    (void)period;
#endif
}
} // namespace Util

#endif

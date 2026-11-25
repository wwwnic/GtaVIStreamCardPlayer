#include "platform/gpu_priority.h"

#include <windows.h>

#include "platform/log.h"

namespace gp {
namespace {
// D3DKMT_SCHEDULINGPRIORITYCLASS (d3dkmthk.h, in the WDK, not the SDK).
enum SchedulingClass : int { kNormal = 2, kHigh = 4 };
using SetClassFn = LONG(APIENTRY*)(HANDLE, int);
using GetClassFn = LONG(APIENTRY*)(HANDLE, int*);
}  // namespace

void RaiseGpuPriority() {
    static bool done = false;  // once per run; a device rebuild keeps the process class
    if (done) return;
    HMODULE gdi = LoadLibraryExW(L"gdi32.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    auto set = gdi ? reinterpret_cast<SetClassFn>(GetProcAddress(gdi, "D3DKMTSetProcessSchedulingPriorityClass")) : nullptr;
    auto get = gdi ? reinterpret_cast<GetClassFn>(GetProcAddress(gdi, "D3DKMTGetProcessSchedulingPriorityClass")) : nullptr;
    if (!set || !get) {
        Log("GPU priority: not available on this Windows");
        return;
    }
    int before = -1;
    const LONG query = get(GetCurrentProcess(), &before);
    if (query == 0 && before >= kHigh) {
        done = true;
        Log("GPU priority: already class %d", before);
        return;
    }
    const LONG status = set(GetCurrentProcess(), kHigh);
    int after = -1;
    get(GetCurrentProcess(), &after);
    done = status == 0 && after == kHigh;
    Log("GPU priority: %s (class %d -> %d, status 0x%08lX)", done ? "High" : "unchanged", before, after,
        static_cast<unsigned long>(status));
}

}  // namespace gp

#include "platform/keep_awake.h"

#include <powrprof.h>

#include "platform/log.h"

#pragma comment(lib, "powrprof.lib")

namespace gp {

namespace {

// What Windows as a whole currently holds (all processes), for the log.
std::string WindowsHolds() {
    ULONG state = 0;
    if (CallNtPowerInformation(SystemExecutionState, nullptr, 0, &state, sizeof state) != 0) return "unknown";
    return ExecutionStateText(state);
}

}  // namespace

KeepAwake::~KeepAwake() {
    Release("exit");
    if (displayNotification_) UnregisterPowerSettingNotification(displayNotification_);
}

void KeepAwake::Start(HWND window) {
    if (displayNotification_) return;
    displayNotification_ = RegisterPowerSettingNotification(window, &GUID_CONSOLE_DISPLAY_STATE,
                                                            DEVICE_NOTIFY_WINDOW_HANDLE);
    if (!displayNotification_) Log("Stay awake: no display state notifications (error %lu)", GetLastError());
}

void KeepAwake::Second(bool framesArrived) {
    switch (state_.Second(framesArrived)) {
    case StayAwake::Change::Take:
        Take();
        break;
    case StayAwake::Change::Release:
        Release("no Feed playing for 3 s");
        break;
    case StayAwake::Change::None:
        break;
    }
    if (!requested_) return;
    // Belt and braces: reset the system and display idle timers each second
    // too (no ES_CONTINUOUS: leaves the continuous hold as it is).
    SetThreadExecutionState(ES_SYSTEM_REQUIRED | ES_DISPLAY_REQUIRED);
    if (++heldSeconds_ % 60 == 0) {
        Log("Stay awake: still held after %d min; Windows holds %s", heldSeconds_ / 60, WindowsHolds().c_str());
    }
}

void KeepAwake::Take() {
    if (requested_) return;
    bool powerRequest = false;
    REASON_CONTEXT reason = {POWER_REQUEST_CONTEXT_VERSION, POWER_REQUEST_CONTEXT_SIMPLE_STRING};
    reason.Reason.SimpleReasonString = const_cast<LPWSTR>(L"GamePlayer: a Feed is playing");
    request_ = PowerCreateRequest(&reason);
    if (request_ == INVALID_HANDLE_VALUE) request_ = nullptr;
    if (request_) {
        // Each failure logged at once, with its own error code.
        const bool display = PowerSetRequest(request_, PowerRequestDisplayRequired) != FALSE;
        if (!display) Log("Stay awake: PowerSetRequest display failed (error %lu)", GetLastError());
        const bool system = PowerSetRequest(request_, PowerRequestSystemRequired) != FALSE;
        if (!system) Log("Stay awake: PowerSetRequest system failed (error %lu)", GetLastError());
        powerRequest = display && system;
    } else {
        Log("Stay awake: PowerCreateRequest failed (error %lu)", GetLastError());
    }
    const bool threadState = SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED | ES_DISPLAY_REQUIRED) != 0;
    if (!threadState) Log("Stay awake: SetThreadExecutionState failed");
    requested_ = true;
    heldSeconds_ = 0;
    Log("Stay awake: taken (Feed playing): system and display sleep off; power request %s, thread state %s; "
        "Windows holds %s",
        powerRequest ? "yes" : "no", threadState ? "yes" : "no", WindowsHolds().c_str());
}

void KeepAwake::Release(const char* why) {
    if (!requested_) return;
    if (request_) {
        PowerClearRequest(request_, PowerRequestDisplayRequired);
        PowerClearRequest(request_, PowerRequestSystemRequired);
        CloseHandle(request_);
        request_ = nullptr;
    }
    SetThreadExecutionState(ES_CONTINUOUS);
    requested_ = false;
    Log("Stay awake: released (%s): normal sleep behaviour after %d s held", why, heldSeconds_);
}

bool KeepAwake::RefuseSysCommand(WPARAM command) const {
    return requested_ && (command & 0xFFF0) == SC_SCREENSAVE;
}

void KeepAwake::PowerBroadcast(WPARAM wParam, LPARAM lParam) {
    if (wParam != PBT_POWERSETTINGCHANGE || !lParam) return;
    const auto* setting = reinterpret_cast<const POWERBROADCAST_SETTING*>(lParam);
    if (setting->PowerSetting != GUID_CONSOLE_DISPLAY_STATE || setting->DataLength < sizeof(DWORD)) return;
    const DWORD display = *reinterpret_cast<const DWORD*>(setting->Data);
    Log("%s; Windows holds %s", DisplayStateLine(display, requested_).c_str(), WindowsHolds().c_str());
}

}  // namespace gp

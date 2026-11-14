#include "platform/keep_awake.h"

#include "platform/log.h"

namespace gp {

void KeepAwake::Second(bool framesArrived) {
    switch (state_.Second(framesArrived)) {
    case StayAwake::Change::Take:
        if (SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED | ES_DISPLAY_REQUIRED)) {
            requested_ = true;
            Log("Stay awake: taken (Feed playing): system and display sleep off");
        } else {
            Log("Stay awake: SetThreadExecutionState failed");
        }
        break;
    case StayAwake::Change::Release:
        Release("no Feed playing for 3 s");
        break;
    case StayAwake::Change::None:
        break;
    }
}

void KeepAwake::Release(const char* why) {
    if (!requested_) return;
    SetThreadExecutionState(ES_CONTINUOUS);
    requested_ = false;
    Log("Stay awake: released (%s): normal sleep behaviour", why);
}

bool KeepAwake::RefuseSysCommand(WPARAM command) const {
    return requested_ && (command & 0xFFF0) == SC_SCREENSAVE;
}

}  // namespace gp

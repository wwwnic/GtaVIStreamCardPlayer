#include "core/stay_awake.h"

namespace gp {

StayAwake::Change StayAwake::Second(bool framesArrived) {
    if (framesArrived) {
        quietSeconds_ = 0;
        if (held_) return Change::None;
        held_ = true;
        return Change::Take;
    }
    if (!held_) return Change::None;
    if (++quietSeconds_ < kReleaseAfterQuietSeconds) return Change::None;
    held_ = false;
    quietSeconds_ = 0;
    return Change::Release;
}

}  // namespace gp

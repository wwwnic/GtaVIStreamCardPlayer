// When to keep the PC and display awake: while a Feed plays, because
// controller input on the Console is not PC activity. Released a few quiet
// seconds after frames stop, so a brief gap does not flap the request.
#pragma once

namespace gp {

class StayAwake {
public:
    static constexpr int kReleaseAfterQuietSeconds = 3;
    enum class Change { None, Take, Release };

    // Call once a second: did any frame arrive during that second?
    Change Second(bool framesArrived);
    bool Held() const { return held_; }

private:
    bool held_ = false;
    int quietSeconds_ = 0;
};

}  // namespace gp

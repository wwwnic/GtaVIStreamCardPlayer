// Feedback: a short confirmation line ("Volume 80%") at the top of the Video
// Area, gone 1.5 seconds after the last one (FeedbackLine::kShowMs). A new
// line replaces the old one in place.
//
// Drawn by the Presenter (ShowText, TextPlace::Top). The Presenter presents
// on its own when a text appears or ends, so the line shows and clears on
// time even when the picture is static, black or absent.
#pragma once

#include <string>

#include "platform/presenter.h"

namespace gp {

class Feedback {
public:
    explicit Feedback(Presenter& presenter) : presenter_(presenter) {}

    // Shows the line (plain ASCII/UTF-8) now. Any thread.
    void Show(const std::string& line);

private:
    Presenter& presenter_;
};

}  // namespace gp

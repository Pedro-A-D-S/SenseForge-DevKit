// dualsense_input.cpp — PS+Options combo + edge detection (SF-6).

#include "dualsense_input.hpp"

namespace senseforge {

bool DualsenseNav::Update(const ControllerButtons& now) {
    prev_ = current_;
    current_ = now;

    // PS + Options combo: must be held together for kComboHoldTicks and
    // fires once — releasing either button re-arms the gesture.
    if (now.ps && now.options) {
        ++combo_ticks_;
    } else {
        combo_ticks_ = 0;
        combo_fired_ = false;
    }

    if (combo_ticks_ >= kComboHoldTicks && !combo_fired_) {
        combo_fired_ = true;
        return true;
    }
    return false;
}

}  // namespace senseforge

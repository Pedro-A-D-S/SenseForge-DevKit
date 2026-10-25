// dualsense_input.hpp — controller-side input for the in-game overlay
// (SF-6). The DualSense PS button never reaches the game through XInput,
// so the overlay toggle (PS + Options) and the Sony-style D-Pad/X/Circle
// navigation are read straight from the HID input state exposed by the
// vendored dualsensitive core ( HidController::TryReadInput ).
//
// Pure state machine over a plain button snapshot — hardware-free and
// unit-testable; HidController owns the actual HID read.

#pragma once

#include <string>
#include <vector>

namespace senseforge {

// Normalized DualSense buttons relevant to the overlay (SF-6). Field
// semantics come from the vendored DS5W input defines:
//   PS        = DS5W_ISTATE_BTN_B_PLAYSTATION_LOGO (buttonsB)
//   Options   = DS5W_ISTATE_BTN_A_MENU             (buttonsA)
//   Cross/X   = DS5W_ISTATE_BTX_CROSS, Circle = DS5W_ISTATE_BTX_CIRCLE
//   D-Pad     = DS5W_ISTATE_DPAD_*                 (buttonsAndDpad)
struct ControllerButtons {
    bool ps = false;
    bool options = false;
    bool cross = false;
    bool circle = false;
    bool dpad_up = false;
    bool dpad_down = false;
    bool dpad_left = false;
    bool dpad_right = false;
};

// Edge/combo detector fed once per worker tick (~50Hz):
//   * PS+Options held together for >= kComboHoldTicks fires the overlay
//     toggle ONCE per combo press (releasing either button re-arms it) —
//     the Sony PS5 quick-menu gesture;
//   * held button snapshots are exposed for ImGui gamepad navigation
//     (ImGui does its own key-repeat, so no edge events needed there).
class DualsenseNav {
public:
    static constexpr int kComboHoldTicks = 3;  // ~60ms @50Hz

    // Returns true exactly when the PS+Options toggle fires.
    bool Update(const ControllerButtons& now);

    const ControllerButtons& current() const { return current_; }

private:
    ControllerButtons current_;
    ControllerButtons prev_;
    int combo_ticks_ = 0;
    bool combo_fired_ = false;
};

}  // namespace senseforge

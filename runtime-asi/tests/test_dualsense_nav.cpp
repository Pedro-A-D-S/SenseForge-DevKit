// test_dualsense_nav.cpp — PS+Options combo state machine (SF-6): held
// combo fires once, re-arms on release, never fires on partial presses.

#include "test_common.hpp"

#include "dualsense_input.hpp"

using namespace senseforge;

int main() {
    DualsenseNav nav;

    // Partial presses never fire (Options alone, PS alone).
    {
        ControllerButtons options_only;
        options_only.options = true;
        for (int i = 0; i < 20; ++i) {
            assert(!nav.Update(options_only));
        }
        ControllerButtons ps_only;
        ps_only.ps = true;
        for (int i = 0; i < 20; ++i) {
            assert(!nav.Update(ps_only));
        }
    }

    // Combo fires once after kComboHoldTicks, then re-arms on release.
    {
        DualsenseNav local;
        ControllerButtons combo;
        combo.ps = true;
        combo.options = true;

        for (int i = 0; i < DualsenseNav::kComboHoldTicks - 1; ++i) {
            assert(!local.Update(combo));  // arming, not yet fired
        }
        assert(local.Update(combo));  // fires exactly here
        assert(!local.Update(combo));  // ...and never again while held
        assert(!local.Update(combo));

        // Release Options -> combo re-arms.
        ControllerButtons ps_only;
        ps_only.ps = true;
        assert(!local.Update(ps_only));
        assert(!local.Update(ps_only));

        for (int i = 0; i < DualsenseNav::kComboHoldTicks - 1; ++i) {
            assert(!local.Update(combo));
        }
        assert(local.Update(combo));  // second press fires again
    }

    // current() mirrors the last snapshot (ImGui navigation source).
    {
        DualsenseNav local;
        ControllerButtons nav_buttons;
        nav_buttons.dpad_up = true;
        nav_buttons.cross = true;
        assert(!local.Update(nav_buttons));
        assert(local.current().dpad_up);
        assert(local.current().cross);
        assert(!local.current().circle);
    }

    return 0;
}

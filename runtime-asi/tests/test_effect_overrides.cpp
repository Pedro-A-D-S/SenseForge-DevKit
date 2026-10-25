// test_effect_overrides.cpp — SF-14 per-effect overrides written by the
// ReShade panel: [States] enable/force overrides applied by ComposeCommand
// and the state selection helper, and [EventEffects] enable / intensity /
// lightbar applied by ApplyEventEffect.

#include "test_common.hpp"

#include "hid_controller.hpp"
#include "ini_config.hpp"

using namespace senseforge;

int main() {
    Manifest manifest;
    State free_aim;
    free_aim.id = "free_aim";
    free_aim.left_trigger.mode = TriggerMode::Resistance;
    free_aim.left_trigger.start_position = 1;
    free_aim.left_trigger.end_position = 6;
    free_aim.left_trigger.force = 5;
    free_aim.right_trigger.mode = TriggerMode::Off;
    const BindingValues values;

    // ---- States -------------------------------------------------------
    {
        IniDocument ini;  // no overrides: manifest values, state enabled
        assert(StateEnabled(ini, "free_aim"));
        assert(StateForceOverride(ini, "free_aim", "left") == 0);
        const HidOutputCommand command =
            ComposeCommand(manifest, free_aim, values, ini);
        assert(command.left_mode == TriggerEffectMode::Resistance);
        assert(command.left.force == 5);
    }
    {
        IniDocument ini;
        ini.SetBool("States", "free_aim_enabled", false);
        ini.SetInt("States", "free_aim_left_force", 7);
        ini.SetInt("States", "free_aim_right_force", 0);  // 0 = manifest
        assert(!StateEnabled(ini, "free_aim"));
        assert(StateEnabled(ini, "combat"));  // other states untouched
        const HidOutputCommand command =
            ComposeCommand(manifest, free_aim, values, ini);
        assert(command.left.force == 7);
        assert(command.right_mode == TriggerEffectMode::Off);
    }
    {
        IniDocument ini;  // out of the encoder range: ignored (fail-closed)
        ini.SetInt("States", "free_aim_left_force", 12);
        assert(StateForceOverride(ini, "free_aim", "left") == 0);
        assert(ComposeCommand(manifest, free_aim, values, ini).left.force ==
               5);
    }

    // ---- Events -------------------------------------------------------
    EventEffect parry;
    parry.id = "parry";
    parry.lightbar_rgb = {255, 215, 0};
    parry.motor_left = 200;
    parry.motor_right = 100;
    parry.trigger_amplitude = 6;
    parry.trigger_frequency_hz = 45;
    parry.present = true;
    {
        IniDocument ini;  // defaults: full effect at k=1
        HidOutputCommand command;
        assert(ApplyEventEffect(parry, 1.0, ini, command));
        assert(command.lightbar_rgb[0] == 255 && command.lightbar_rgb[1] == 215);
        assert(command.motor_left == 200 && command.motor_right == 100);
        assert(command.left_mode == TriggerEffectMode::Vibration);
        assert(command.left.amplitude == 6);
        assert(command.left.frequency_hz == 45);
    }
    {
        IniDocument ini;  // 50%, lightbar off: state colour kept
        ini.SetInt("EventEffects", "parry_intensity", 50);
        ini.SetBool("EventEffects", "parry_lightbar", false);
        HidOutputCommand command;
        command.lightbar_rgb[2] = 77;  // what the state engine produced
        assert(ApplyEventEffect(parry, 1.0, ini, command));
        assert(command.lightbar_rgb[0] == 0 && command.lightbar_rgb[2] == 77);
        assert(command.motor_left == 100 && command.motor_right == 50);
        assert(command.left.amplitude == 3);
    }
    {
        IniDocument ini;  // 150% clamps the motors at 255, amplitude at 8
        ini.SetInt("EventEffects", "parry_intensity", 150);
        HidOutputCommand command;
        assert(ApplyEventEffect(parry, 1.0, ini, command));
        assert(command.motor_left == 255);
        assert(command.motor_right == 150);
        assert(command.left.amplitude == 8);
    }
    {
        IniDocument ini;  // 0%: lightbar only, no rumble, no trigger pulse
        ini.SetInt("EventEffects", "parry_intensity", 0);
        HidOutputCommand command;
        assert(ApplyEventEffect(parry, 1.0, ini, command));
        assert(command.lightbar_rgb[0] == 255);
        assert(command.motor_left == 0);
        assert(command.left_mode == TriggerEffectMode::Off);
    }
    {
        IniDocument ini;  // disabled: command untouched
        ini.SetBool("EventEffects", "parry_enabled", false);
        assert(!EventEnabled(ini, "parry"));
        HidOutputCommand command;
        command.motor_left = 9;
        assert(!ApplyEventEffect(parry, 1.0, ini, command));
        assert(command.motor_left == 9 && command.lightbar_rgb[0] == 0);
    }
    return 0;
}

// test_ini.cpp — senseforge_user.ini parser round-trip (SF-3).
// Fixture: the exact sample of SENSEFORGE_SPEC.md §3.2.

#include "test_common.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>

#include "ini_config.hpp"

namespace {

std::filesystem::path WriteFixture(const std::string& name,
                                   const std::string& content) {
    const auto path = std::filesystem::temp_directory_path() / name;
    std::ofstream out(path, std::ios::trunc);
    out << content;
    return path;
}

const char* kSampleIni = R"INI([General]
enable_mod=true
auto_disable_triggers_in_menus=true
overlay_toggle_key=0x74 ; VK_F5
controller_chord_overlay=true ; PS_Button + Options

[Haptics]
; 0 = Off, 1 = Weak (0.4), 2 = Medium (0.7), 3 = Strong (1.0), 4 = Boost (1.3)
haptic_intensity_preset=3
custom_haptic_gain=1.00
suppress_xinput_rumble=true
soft_limiter_ceiling=0.92

[AdaptiveTriggers]
left_trigger_intensity=3
right_trigger_intensity=3

[ControllerSpeaker]
speaker_enabled=true
speaker_volume=0.75

[Lighting]
lightbar_mode=1
static_color_rgb=0,112,209
player_leds_mode=2
)INI";

}  // namespace

int main() {
    senseforge::IniDocument ini;
    assert(ini.Load(WriteFixture("sf_test_user.ini", kSampleIni)));

    // Types, including the hex VK code with trailing comment.
    assert(ini.GetBool("General", "enable_mod") == true);
    assert(ini.GetInt("General", "overlay_toggle_key") == 0x74);
    assert(ini.GetBool("Haptics", "suppress_xinput_rumble") == true);
    assert(ini.GetInt("Haptics", "haptic_intensity_preset") == 3);
    assert(ini.GetFloat("Haptics", "custom_haptic_gain") == 1.00);
    assert(ini.GetFloat("Haptics", "soft_limiter_ceiling") == 0.92);
    assert(ini.GetFloat("ControllerSpeaker", "speaker_volume") == 0.75);
    assert(ini.GetString("Lighting", "static_color_rgb") == "0,112,209");
    assert(ini.GetBool("Lighting", "missing_key", true) == true);  // fallback
    assert(!ini.HasKey("General", "nope"));

    // Round-trip: edits survive a save + reload.
    ini.SetInt("Haptics", "haptic_intensity_preset", 4);
    ini.SetBool("General", "enable_mod", false);
    const auto saved = std::filesystem::temp_directory_path() /
                        "sf_test_user_saved.ini";
    assert(ini.Save(saved));
    senseforge::IniDocument reloaded;
    assert(reloaded.Load(saved));
    assert(reloaded.GetInt("Haptics", "haptic_intensity_preset") == 4);
    assert(reloaded.GetBool("General", "enable_mod") == false);
    assert(reloaded.GetFloat("ControllerSpeaker", "speaker_volume") == 0.75);

    // SF-22: Set refuses anything that would change the file structure on
    // the next Load, and leaves the document untouched.
    {
        senseforge::IniDocument evil;
        assert(!evil.Set("States", "x\n[Dev]\nagent_autostart=1\n;", "1"));
        assert(!evil.Set("States", "a=b", "1"));
        assert(!evil.Set("States", "[Dev]", "1"));
        assert(!evil.Set("States", "a;b", "1"));
        assert(!evil.Set("States", "", "1"));
        assert(!evil.Set("States", "k", "1\n[Dev]"));
        assert(!evil.Set("States", "k", "1\r"));
        assert(!evil.Set("St[ates", "k", "1"));
        assert(!evil.Set("St\nates", "k", "1"));
        assert(!evil.Set("", "k", "1"));
        assert(evil.Sections().empty());
        // Ordinary keys, dotted preset keys and values with '=' still work.
        assert(evil.Set("States", "combat_enabled", "true"));
        assert(evil.Set("Preset.1", "States.combat_enabled", "0.5"));
        assert(evil.Set("Events", "file", "a=b.ndjson"));
        assert(evil.GetString("Events", "file") == "a=b.ndjson");
    }

    std::filesystem::remove(std::filesystem::temp_directory_path() /
                            "sf_test_user.ini");
    std::filesystem::remove(saved);
    return 0;
}

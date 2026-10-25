// test_overlay_settings.cpp — ini round-trip of the overlay model (SF-6):
// Load -> mutate -> ApplyTo -> save -> reload must reproduce every value,
// including manifest categories and the gain derived from the preset.

#include "test_common.hpp"

#include <filesystem>
#include <fstream>

#include "ini_config.hpp"
#include "overlay_settings.hpp"

using namespace senseforge;

namespace {

void WriteFixture(const std::filesystem::path& path, const char* text) {
    std::ofstream out(path, std::ios::trunc);
    out << text;
}

}  // namespace

int main() {
    const auto dir = std::filesystem::temp_directory_path();
    const auto ini_path = dir / "sf_test_overlay_settings.ini";

    WriteFixture(ini_path,
                 "[AdaptiveTriggers]\n"
                 "left_trigger_intensity = 2\n"
                 "[ControllerSpeaker]\n"
                 "speaker_volume = 0.5\n"
                 "[Haptics]\n"
                 "custom_haptic_gain = 1.3\n"
                 "[Lighting]\n"
                 "lightbar_mode = 2\n");

    const std::vector<std::string> categories = {
        "bow_and_ranged", "melee_combat", "mount_and_ship"};

    IniDocument ini;
    assert(ini.Load(ini_path));

    // ---- Load: explicit keys win, absent keys default -----------------
    OverlaySettings settings;
    settings.Load(ini, categories);
    assert(settings.left_trigger_intensity == 2);
    assert(std::fabs(settings.speaker_volume - 0.5) < 1e-6);
    assert(settings.lightbar_mode == 2);
    assert(settings.right_trigger_intensity == 3);   // default Strong
    assert(settings.disable_triggers_in_menus);       // default true
    assert(settings.lightbar_brightness == 1);
    assert(settings.player_leds_mode == 2);           // adrenaline segments
    assert(settings.categories.size() == 3);
    assert(settings.categories.at("melee_combat"));   // default enabled
    assert(!settings.categories.at("bow_and_ranged") == false);

    // haptic_intensity defaults to 3 when absent (custom_haptic_gain 1.3
    // is a Boost-era value from another tool — the preset key rules).
    assert(settings.haptic_intensity == 3);

    // ---- Mutate + ApplyTo: every overlay-owned key lands in the ini ----
    settings.haptic_intensity = 4;              // Boost 130%
    settings.right_trigger_intensity = 0;       // Off
    settings.disable_triggers_in_menus = false;
    settings.speaker_enabled = false;
    settings.speaker_volume = 0.42;
    settings.sound_parry = false;
    settings.lightbar_mode = 0;                 // Off
    settings.lightbar_brightness = 2;
    settings.player_leds_mode = 1;               // Player number
    settings.categories.at("melee_combat") = false;

    settings.ApplyTo(ini);
    assert(ini.Save(ini_path));

    // Round-trip: fresh document, fresh settings.
    IniDocument reloaded;
    assert(reloaded.Load(ini_path));
    OverlaySettings round;
    round.Load(reloaded, categories);

    assert(round.haptic_intensity == 4);
    assert(std::fabs(OverlaySettings::HapticGainFor(4) - 1.3) < 1e-9);
    // ApplyTo must ALSO have written the derived gain for the worker.
    const double gain = reloaded.GetFloat("Haptics", "custom_haptic_gain",
                                          -1.0);
    assert(std::fabs(gain - 1.3) < 1e-6);
    assert(round.right_trigger_intensity == 0);
    assert(!round.disable_triggers_in_menus);
    assert(!round.speaker_enabled);
    assert(std::fabs(round.speaker_volume - 0.42) < 1e-6);
    assert(!round.sound_parry);
    assert(round.sound_mount);                  // untouched stays true
    assert(round.lightbar_mode == 0);
    assert(round.lightbar_brightness == 2);
    assert(round.player_leds_mode == 1);
    assert(!round.categories.at("melee_combat"));
    assert(round.categories.at("mount_and_ship"));

    // ---- Equality: snapshot diff the overlay uses to detect changes ----
    assert(round.Equals(round));
    OverlaySettings tweaked = round;
    tweaked.haptic_intensity = 0;
    assert(!round.Equals(tweaked));

    // Preset table bounds.
    assert(OverlaySettings::HapticGainFor(0) == 0.0);
    assert(OverlaySettings::HapticGainFor(1) == 0.4);
    assert(OverlaySettings::HapticGainFor(3) == 1.0);

    // ---- SF-14: per-effect overrides, profiles, preset slots -------------
    {
        const std::vector<std::string> state_ids = {"combat", "free_aim"};
        const std::vector<std::string> event_ids = {"parry", "hit_taken"};
        IniDocument fixture;
        fixture.SetInt("AdaptiveTriggers", "left_trigger_intensity", 4);  // Max
        fixture.SetBool("States", "free_aim_enabled", false);
        fixture.SetInt("States", "free_aim_left_force", 6);
        fixture.SetInt("EventEffects", "hit_taken_intensity", 40);
        fixture.SetBool("EventEffects", "parry_lightbar", false);

        OverlaySettings s;
        s.Load(fixture, {}, state_ids, event_ids);
        assert(s.left_trigger_intensity == 4);  // Maximo survives Load
        assert(!s.states["free_aim"].enabled);
        assert(s.states["free_aim"].left_force == 6);
        assert(s.states["combat"].enabled && s.states["combat"].left_force == 0);
        assert(s.events["hit_taken"].intensity == 40);
        assert(!s.events["parry"].lightbar);

        // Round trip through ApplyTo.
        IniDocument written;
        s.ApplyTo(written);
        OverlaySettings back;
        back.Load(written, {}, state_ids, event_ids);
        assert(back.Equals(s));

        // Profiles fill the base values and clear force overrides.
        s.ApplyProfile(kProfileWeak);
        assert(s.profile == kProfileWeak);
        assert(s.left_trigger_intensity == 1 && s.right_trigger_intensity == 1);
        assert(s.haptic_intensity == 1);
        assert(s.events["hit_taken"].intensity == 50);
        assert(s.states["free_aim"].left_force == 0);
        assert(!s.states["free_aim"].enabled);  // switches are kept
        s.ApplyProfile(kProfileMedium);
        assert(s.events["parry"].intensity == 75);
        s.ApplyProfile(kProfileCustom);  // not a base profile: no-op
        assert(s.profile == kProfileMedium);

        // SF-17: a MOD may override the bases of a profile.
        UiProfile mod_weak;
        mod_weak.trigger_preset = 2;
        mod_weak.event_intensity = 40;  // haptic_preset left at -1
        s.ApplyProfile(kProfileWeak, &mod_weak);
        assert(s.left_trigger_intensity == 2 && s.right_trigger_intensity == 2);
        assert(s.haptic_intensity == 1);  // framework default kept
        assert(s.events["parry"].intensity == 40);
        assert(s.profile == kProfileWeak);
        s.ApplyProfile(kProfileMedium);  // back to framework defaults

        // Preset slots are independent of each other and of the live keys.
        IniDocument doc;
        OverlaySettings slot1 = s;
        slot1.SavePreset(doc, 1);
        OverlaySettings slot2 = s;
        slot2.ApplyProfile(kProfileStrong);
        slot2.events["parry"].enabled = false;
        slot2.SavePreset(doc, 2);
        assert(!doc.HasSection("EventEffects"));  // presets only

        OverlaySettings loaded;
        assert(loaded.LoadPreset(doc, 1, {}, state_ids, event_ids));
        assert(loaded.Equals(slot1));
        assert(loaded.LoadPreset(doc, 2, {}, state_ids, event_ids));
        assert(loaded.Equals(slot2));
        assert(!loaded.events["parry"].enabled);
        assert(!loaded.LoadPreset(doc, 3, {}, state_ids, event_ids));
        assert(loaded.Equals(slot2));  // failed load leaves it untouched
    }

    // Display names: known pretty, unknown raw.
    assert(CategoryDisplayName("melee_combat") ==
           "Combate Corpo a Corpo");
    assert(CategoryDisplayName("custom_mod_category") ==
           "custom_mod_category");

    return 0;
}

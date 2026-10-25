// overlay_settings.hpp — the settings model behind the in-game overlay
// (SF-6), per skill:sony-style-imgui-overlay. One struct, two directions:
//   Load()    — reads senseforge_user.ini (plus the manifest categories)
//   ApplyTo() — writes every overlay-owned key back into the ini
// The overlay renders these fields; after any change it applies + saves
// the ini, and the runtime's hot-reload watcher picks the new values up
// live (<10ms) — no restart, no extra IPC.

#pragma once

#include <map>
#include <string>
#include <vector>

#include "ini_config.hpp"
#include "sense_json.hpp"  // UiProfile (SF-17)

namespace senseforge {

// SF-14 per-state override ([States] <id>_enabled / _left_force /
// _right_force). Force 0 = keep the manifest value.
struct StateOverride {
    bool enabled = true;
    int left_force = 0;   // 0 or 1-8
    int right_force = 0;  // 0 or 1-8
    bool operator==(const StateOverride&) const = default;
};

// SF-14 per-event override ([EventEffects] <id>_enabled / _intensity /
// _lightbar).
struct EventOverride {
    bool enabled = true;
    int intensity = 100;  // % of rumble + trigger pulse, 0-150
    bool lightbar = true;
    bool operator==(const EventOverride&) const = default;
};

// SF-14 intensity profile inside a preset (the RenoDX "tone mapper" slot):
// 0 = Personalizado (edited by hand), 1 = Fraco, 2 = Medio, 3 = Forte.
enum IntensityProfile : int {
    kProfileCustom = 0,
    kProfileWeak = 1,
    kProfileMedium = 2,
    kProfileStrong = 3,
};

// SF-14 preset slots of the ReShade panel ([SenseForge] active_preset).
// 0 = Desligado (master switch off), 1-3 = user presets.
constexpr int kPresetOff = 0;
constexpr int kPresetCount = 3;

struct OverlaySettings {
    // [Haptics] — HD Haptics intensity presets (spec §3.2):
    // 0=Off 1=40% 2=70% 3=100% 4=130% (Boost).
    int haptic_intensity = 3;
    // [AdaptiveTriggers] 0=Off 1=Weak 2=Medium 3=Strong 4=Maximum (SF-14,
    // 1.25x the manifest force) — per trigger.
    int left_trigger_intensity = 3;
    int right_trigger_intensity = 3;
    bool disable_triggers_in_menus = true;

    // [ControllerSpeaker]
    bool speaker_enabled = true;
    double speaker_volume = 0.75;  // 0.0-1.0
    bool sound_parry = true;
    bool sound_mount = true;
    bool sound_ui = true;

    // [Lighting] — semantics already consumed by ComposeCommand (SF-4):
    // lightbar_mode: 0=Off 1=Dynamic 2=Static (default 1)
    // lightbar_brightness: 0=OLED dim 1=Medium 2=Bright
    // player_leds_mode: 0=Off 1=Player number 2=Adrenaline segments
    int lightbar_mode = 1;
    int lightbar_brightness = 1;
    int player_leds_mode = 2;

    // [Categories] — per-game dynamic toggles from {game}.sense.json.
    std::map<std::string, bool> categories;

    // SF-14 per-effect overrides, keyed by manifest state / event id.
    std::map<std::string, StateOverride> states;
    std::map<std::string, EventOverride> events;
    // [SenseForge] profile — see IntensityProfile.
    int profile = kProfileStrong;

    // Reads the ini (categories default to enabled when absent; states and
    // events default to the manifest behaviour when absent).
    void Load(const IniDocument& ini,
              const std::vector<std::string>& category_ids,
              const std::vector<std::string>& state_ids = {},
              const std::vector<std::string>& event_ids = {});

    // Writes every overlay-owned key into the document (preserving all
    // foreign sections/keys — IniDocument::Set only touches these).
    void ApplyTo(IniDocument& ini) const;

    bool Equals(const OverlaySettings& other) const;

    // SF-14: fills the base values of a profile (Fraco/Medio/Forte):
    // trigger + haptic presets = profile, every event intensity
    // 50/75/100%, force overrides cleared (manifest values). Enabled
    // switches, categories, speaker and lighting are left alone.
    // SF-17: a MOD may override those bases in its manifest ("ui" ->
    // "profiles"); fields left at -1 keep the framework default.
    void ApplyProfile(int profile_id, const UiProfile* mod_base = nullptr);

    // SF-14 preset slots: [Preset.N] stores this whole model as
    // "<Section>.<key>" entries (the same keys ApplyTo writes), so slots
    // stay independent of the live sections the runtime reads.
    void SavePreset(IniDocument& ini, int slot) const;
    // Returns false (and leaves *this untouched) when the slot was never
    // saved.
    bool LoadPreset(const IniDocument& ini, int slot,
                    const std::vector<std::string>& category_ids,
                    const std::vector<std::string>& state_ids,
                    const std::vector<std::string>& event_ids);

    // Preset table for [Haptics] custom_haptic_gain.
    static double HapticGainFor(int intensity);
};

// "Preset.N" section name for slot N (1-kPresetCount).
std::string PresetSection(int slot);

// Pretty display name for a manifest category id (spec §3.1 categories;
// unknown ids fall back to the raw id).
std::string CategoryDisplayName(const std::string& category_id);

}  // namespace senseforge

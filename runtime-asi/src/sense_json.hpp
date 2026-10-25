// sense_json.hpp — {game}.sense.json manifest model + parser (SF-3).
//
// SENSEFORGE_SPEC.md §3.1 schema, parsed with nlohmann/json (pinned in
// tools/fetch_third_party.sh). Hot-reloaded live by the Shadow Play agent
// (SF-11): every field is optional-tolerant so an in-progress edit by the
// RE agent can never hard-crash the runtime — a bad file just keeps the
// previously loaded manifest.

#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "memory_scanner.hpp"  // BindingType

namespace senseforge {

enum class TriggerMode {
    Off,
    Resistance,        // constant force from start_position (spec "resistance")
    RigidSection,      // hard wall between start/end (spec "rigid_section")
    WeaponSnap,        // weapon-like snap (spec "weapon_snap")
    BowElasticScaled,  // scaled bow draw (spec "bow_elastic_scaled")
    Vibration,         // raw periodic pulse (spec "full_draw_vibration_hz")
};

struct TriggerEffect {
    TriggerMode mode = TriggerMode::Off;
    int start_position = 0;  // DualSense position units (0-9)
    int end_position = 0;
    int force = 0;            // 0-8
    int base_force = 0;
    int max_force = 0;
    std::string scale_binding;  // memory binding id used to scale the force
    int full_draw_vibration_hz = 0;
    double full_draw_threshold = 0.0;
};

struct HapticLoop {
    std::string file;           // e.g. "haptics/bow_string_creak_ch34.wav"
    std::string gain_binding;    // memory binding id for the gain
    bool present = false;
};

struct State {
    std::string id;
    std::string category;
    int priority = 0;
    std::string condition;  // e.g. "is_menu_or_map_open == 1"
    TriggerEffect left_trigger;
    TriggerEffect right_trigger;
    bool haptics_mute = false;
    HapticLoop haptic_loop;
};

struct TriggerPulse {
    std::string target = "both";  // left | right | both
    int frequency_hz = 0;
    int duration_ms = 0;
    int amplitude = 0;  // 0-8 HID units
    bool present = false;
};

struct AudioEventTrigger {
    std::string event_id;
    std::string category;
    std::string wwise_event_hash;  // e.g. "0x8F41A20C"
    std::string speaker_wav;
    std::string haptic_wav;
    TriggerPulse trigger_pulse;
};

struct LightingRules {
    std::string lightbar_mode = "health_and_adrenaline";
    std::array<int, 3> high_health_rgb{20, 210, 80};
    std::array<int, 3> low_health_rgb{230, 25, 25};
    std::array<int, 3> adrenaline_full_pulse_rgb{255, 195, 0};
    std::string player_leds_binding;
};

// SF-12b: transient reaction to a gameplay event (parry, dodge, gradient)
// reported by the UE4SS Lua bridge over UDP. While active it overlays the
// composed state command: lightbar flash + rumble motors + optional
// trigger pulse, linearly fading over duration_ms.
struct EventEffect {
    std::string id;  // matches the UDP "id" field ("parry", "gradient", ...)
    std::array<int, 3> lightbar_rgb{0, 0, 0};
    int motor_left = 0;    // hard rumble 0-255 at full intensity
    int motor_right = 0;   // soft rumble 0-255 at full intensity
    int trigger_amplitude = 0;      // 0 = no trigger pulse; 1-8 HID units
    int trigger_frequency_hz = 0;   // pulse frequency when amplitude > 0
    int duration_ms = 300;  // fade window (50..5000)
    bool present = false;   // parsed from the manifest (not a wire field)
};

struct GameMetadata {
    std::string game_id;
    std::string title;
    std::string executable;
    std::string engine;
    std::string graphics_api;
};

// SF-17: how one state / event / category shows up in the ReShade panel.
// Every field is optional; absent entries fall back to the raw id.
struct UiEntry {
    std::string name;         // display name
    std::string description;  // tooltip
    bool hidden = false;      // internal (e.g. "exploration"): not listed
    int order = 0;            // ascending; ties keep manifest order
};

// SF-17: base values of one intensity profile (Fraco/Medio/Forte) for
// this MOD. Absent profiles keep the framework defaults.
struct UiProfile {
    int trigger_preset = -1;   // 0-4, -1 = framework default
    int haptic_preset = -1;    // 0-4
    int event_intensity = -1;  // 0-150 %
};

// SF-17: optional "ui" section — the MOD describes its own panel (names,
// tooltips, order, internal entries, profile bases, guide link), so the
// framework never carries game-specific strings (RenoDX-style).
struct ManifestUi {
    std::map<std::string, UiEntry> states;
    std::map<std::string, UiEntry> events;
    std::map<std::string, UiEntry> categories;
    std::array<UiProfile, 4> profiles;  // index = IntensityProfile (1-3)
    std::string guide_url;
    std::string credits;
};

struct Manifest {
    std::string schema_version;
    GameMetadata game_metadata;
    ManifestUi ui;
    // Spec writes "memory_Bindings"; both spellings are accepted.
    std::map<std::string, PointerBinding> memory_bindings;
    std::vector<State> states;
    std::vector<AudioEventTrigger> audio_event_triggers;
    std::vector<EventEffect> event_effects;
    LightingRules lighting_rules;
};

// Parses a manifest. On failure returns false, fills *error, leaves `out`
// untouched (hot-reload safety: caller keeps the previous good manifest).
bool ParseManifest(const std::filesystem::path& path, Manifest& out,
                   std::string* error);

// Structural validation used by the unit tests: every scale_binding /
// gain_binding / condition reference must exist in memory_bindings.
std::vector<std::string> ValidateManifest(const Manifest& manifest);

}  // namespace senseforge

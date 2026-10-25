// overlay_settings.cpp — ini-backed overlay model (SF-6).

#include "overlay_settings.hpp"

#include <algorithm>
#include <cmath>

namespace senseforge {

namespace {
constexpr double kHapticGains[] = {0.0, 0.4, 0.7, 1.0, 1.3};

// Trigger force override: 1-8 (encoder scale), anything else = 0 (manifest).
int ClampForce(long long force) {
    return force >= 1 && force <= 8 ? static_cast<int>(force) : 0;
}
}  // namespace

double OverlaySettings::HapticGainFor(int intensity) {
    if (intensity < 0 || intensity > 4) {
        return 1.0;  // fail-safe: nominal gain
    }
    return kHapticGains[intensity];
}

void OverlaySettings::Load(const IniDocument& ini,
                           const std::vector<std::string>& category_ids,
                           const std::vector<std::string>& state_ids,
                           const std::vector<std::string>& event_ids) {
    haptic_intensity = static_cast<int>(
        ini.GetInt("Haptics", "haptic_intensity", 3));
    if (haptic_intensity < 0 || haptic_intensity > 4) {
        haptic_intensity = 3;
    }
    left_trigger_intensity = static_cast<int>(
        ini.GetInt("AdaptiveTriggers", "left_trigger_intensity", 3));
    right_trigger_intensity = static_cast<int>(
        ini.GetInt("AdaptiveTriggers", "right_trigger_intensity", 3));
    for (int* preset : {&left_trigger_intensity, &right_trigger_intensity}) {
        if (*preset < 0 || *preset > 4) {
            *preset = 3;
        }
    }
    disable_triggers_in_menus = ini.GetBool(
        "AdaptiveTriggers", "disable_in_menus", true);

    speaker_enabled = ini.GetBool("ControllerSpeaker", "enabled", true);
    speaker_volume = ini.GetFloat("ControllerSpeaker", "speaker_volume",
                                  0.75);
    sound_parry = ini.GetBool("ControllerSpeaker", "sound_parry", true);
    sound_mount = ini.GetBool("ControllerSpeaker", "sound_mount", true);
    sound_ui = ini.GetBool("ControllerSpeaker", "sound_ui", true);

    lightbar_mode =
        static_cast<int>(ini.GetInt("Lighting", "lightbar_mode", 1));
    lightbar_brightness =
        static_cast<int>(ini.GetInt("Lighting", "lightbar_brightness", 1));
    player_leds_mode =
        static_cast<int>(ini.GetInt("Lighting", "player_leds_mode", 2));

    categories.clear();
    for (const std::string& id : category_ids) {
        categories[id] = ini.GetBool("Categories", id, true);
    }

    states.clear();
    for (const std::string& id : state_ids) {
        StateOverride& state = states[id];
        state.enabled = ini.GetBool("States", id + "_enabled", true);
        state.left_force = ClampForce(ini.GetInt("States", id + "_left_force", 0));
        state.right_force =
            ClampForce(ini.GetInt("States", id + "_right_force", 0));
    }
    events.clear();
    for (const std::string& id : event_ids) {
        EventOverride& event = events[id];
        event.enabled = ini.GetBool("EventEffects", id + "_enabled", true);
        event.intensity = std::clamp(
            static_cast<int>(ini.GetInt("EventEffects", id + "_intensity", 100)),
            0, 150);
        event.lightbar = ini.GetBool("EventEffects", id + "_lightbar", true);
    }
    profile = std::clamp(
        static_cast<int>(ini.GetInt("SenseForge", "profile", kProfileStrong)),
        0, 3);
}

void OverlaySettings::ApplyTo(IniDocument& ini) const {
    ini.SetInt("Haptics", "haptic_intensity", haptic_intensity);
    // The worker live-refreshes the actual gain from this key (SF-5).
    ini.SetFloat("Haptics", "custom_haptic_gain",
                 HapticGainFor(haptic_intensity));

    ini.SetInt("AdaptiveTriggers", "left_trigger_intensity",
               left_trigger_intensity);
    ini.SetInt("AdaptiveTriggers", "right_trigger_intensity",
               right_trigger_intensity);
    ini.SetBool("AdaptiveTriggers", "disable_in_menus",
                disable_triggers_in_menus);

    ini.SetBool("ControllerSpeaker", "enabled", speaker_enabled);
    ini.SetFloat("ControllerSpeaker", "speaker_volume", speaker_volume);
    ini.SetBool("ControllerSpeaker", "sound_parry", sound_parry);
    ini.SetBool("ControllerSpeaker", "sound_mount", sound_mount);
    ini.SetBool("ControllerSpeaker", "sound_ui", sound_ui);

    ini.SetInt("Lighting", "lightbar_mode", lightbar_mode);
    ini.SetInt("Lighting", "lightbar_brightness", lightbar_brightness);
    ini.SetInt("Lighting", "player_leds_mode", player_leds_mode);

    for (const auto& [id, enabled] : categories) {
        ini.SetBool("Categories", id, enabled);
    }

    for (const auto& [id, state] : states) {
        ini.SetBool("States", id + "_enabled", state.enabled);
        ini.SetInt("States", id + "_left_force", state.left_force);
        ini.SetInt("States", id + "_right_force", state.right_force);
    }
    for (const auto& [id, event] : events) {
        ini.SetBool("EventEffects", id + "_enabled", event.enabled);
        ini.SetInt("EventEffects", id + "_intensity", event.intensity);
        ini.SetBool("EventEffects", id + "_lightbar", event.lightbar);
    }
    ini.SetInt("SenseForge", "profile", profile);
}

void OverlaySettings::ApplyProfile(int profile_id, const UiProfile* mod_base) {
    if (profile_id < kProfileWeak || profile_id > kProfileStrong) {
        return;
    }
    static constexpr int kEventIntensity[] = {100, 50, 75, 100};
    int trigger = profile_id;
    int haptic = profile_id;
    int event_intensity = kEventIntensity[profile_id];
    if (mod_base != nullptr) {
        if (mod_base->trigger_preset >= 0 && mod_base->trigger_preset <= 4) {
            trigger = mod_base->trigger_preset;
        }
        if (mod_base->haptic_preset >= 0 && mod_base->haptic_preset <= 4) {
            haptic = mod_base->haptic_preset;
        }
        if (mod_base->event_intensity >= 0) {
            event_intensity = std::min(mod_base->event_intensity, 150);
        }
    }
    left_trigger_intensity = trigger;
    right_trigger_intensity = trigger;
    haptic_intensity = haptic;
    for (auto& [id, state] : states) {
        state.left_force = 0;
        state.right_force = 0;
    }
    for (auto& [id, event] : events) {
        event.intensity = event_intensity;
    }
    profile = profile_id;
}

std::string PresetSection(int slot) {
    return "Preset." + std::to_string(slot);
}

void OverlaySettings::SavePreset(IniDocument& ini, int slot) const {
    IniDocument flat;
    ApplyTo(flat);
    const std::string section = PresetSection(slot);
    for (const auto& [name, keys] : flat.Sections()) {
        for (const auto& [key, value] : keys) {
            ini.Set(section, name + "." + key, value);
        }
    }
}

bool OverlaySettings::LoadPreset(const IniDocument& ini, int slot,
                                 const std::vector<std::string>& category_ids,
                                 const std::vector<std::string>& state_ids,
                                 const std::vector<std::string>& event_ids) {
    const auto& sections = ini.Sections();
    const auto it = sections.find(PresetSection(slot));
    if (it == sections.end() || it->second.empty()) {
        return false;
    }
    IniDocument flat;
    for (const auto& [dotted, value] : it->second) {
        const std::size_t dot = dotted.find('.');
        if (dot == std::string::npos) {
            continue;
        }
        flat.Set(dotted.substr(0, dot), dotted.substr(dot + 1), value);
    }
    Load(flat, category_ids, state_ids, event_ids);
    return true;
}

bool OverlaySettings::Equals(const OverlaySettings& other) const {
    if (haptic_intensity != other.haptic_intensity ||
        left_trigger_intensity != other.left_trigger_intensity ||
        right_trigger_intensity != other.right_trigger_intensity ||
        disable_triggers_in_menus != other.disable_triggers_in_menus ||
        speaker_enabled != other.speaker_enabled ||
        sound_parry != other.sound_parry ||
        sound_mount != other.sound_mount ||
        sound_ui != other.sound_ui ||
        lightbar_mode != other.lightbar_mode ||
        lightbar_brightness != other.lightbar_brightness ||
        player_leds_mode != other.player_leds_mode ||
        categories != other.categories || states != other.states ||
        events != other.events || profile != other.profile) {
        return false;
    }
    // 1% tolerance: the volume slider is continuous.
    return std::fabs(speaker_volume - other.speaker_volume) < 0.01;
}

std::string CategoryDisplayName(const std::string& category_id) {
    static const std::map<std::string, std::string> kNames = {
        {"bow_and_ranged", "Arco e Armas de Alcance"},
        {"melee_combat", "Combate Corpo a Corpo"},
        {"abilities", "Habilidades Especiais"},
        {"mount_and_ship", "Cavalo Phobos & Navio Adrestia"},
        {"ui", "Menus e Mapa"},
    };
    const auto it = kNames.find(category_id);
    return it != kNames.end() ? it->second : category_id;
}

}  // namespace senseforge

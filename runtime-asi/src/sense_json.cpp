// sense_json.cpp — manifest parsing via nlohmann/json (SF-3).

#include "sense_json.hpp"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

namespace senseforge {
namespace {

using nlohmann::json;

std::uintptr_t ParseHexOrInt(const json& node) {
    if (node.is_string()) {
        const std::string& s = node.get_ref<const std::string&>();
        return static_cast<std::uintptr_t>(
            std::strtoull(s.c_str(), nullptr, 0));  // 0x… auto-detected
    }
    if (node.is_number_unsigned()) {
        return static_cast<std::uintptr_t>(node.get<std::uint64_t>());
    }
    if (node.is_number_integer()) {
        return static_cast<std::uintptr_t>(node.get<std::int64_t>());
    }
    return 0;
}

BindingType ParseType(const std::string& name) {
    if (name == "uint32") {
        return BindingType::UInt32;
    }
    if (name == "float") {
        return BindingType::Float32;
    }
    return BindingType::UInt8;
}

TriggerMode ParseMode(const std::string& mode) {
    if (mode == "resistance") {
        return TriggerMode::Resistance;
    }
    if (mode == "rigid_section") {
        return TriggerMode::RigidSection;
    }
    if (mode == "weapon_snap") {
        return TriggerMode::WeaponSnap;
    }
    if (mode == "bow_elastic_scaled") {
        return TriggerMode::BowElasticScaled;
    }
    if (mode == "vibration") {
        return TriggerMode::Vibration;
    }
    return TriggerMode::Off;
}

TriggerEffect ParseTrigger(const json& node) {
    TriggerEffect effect;
    if (!node.is_object()) {
        return effect;
    }
    effect.mode = ParseMode(node.value("mode", "off"));
    effect.start_position = node.value("start_position", 0);
    effect.end_position = node.value("end_position", 0);
    effect.force = node.value("force", 0);
    effect.base_force = node.value("base_force", 0);
    effect.max_force = node.value("max_force", 0);
    effect.scale_binding = node.value("scale_binding", "");
    effect.full_draw_vibration_hz = node.value("full_draw_vibration_hz", 0);
    effect.full_draw_threshold = node.value("full_draw_threshold", 0.0);
    return effect;
}

PointerBinding ParseBinding(const json& node, std::string* error) {
    PointerBinding binding;
    if (!node.is_object()) {
        if (error) {
            *error = "binding entry is not an object";
        }
        return binding;
    }
    binding.module = node.value("module", "");
    binding.base_rva = ParseHexOrInt(node.value("base_rva", json(0)));
    if (node.contains("pointer_offsets") &&
        node["pointer_offsets"].is_array()) {
        for (const auto& off : node["pointer_offsets"]) {
            binding.pointer_offsets.push_back(ParseHexOrInt(off));
        }
    }
    binding.type = ParseType(node.value("type", "uint8"));
    if (node.contains("expected_range") &&
        node["expected_range"].is_array() &&
        node["expected_range"].size() == 2) {
        binding.has_expected_range = true;
        binding.range_min = node["expected_range"][0].get<double>();
        binding.range_max = node["expected_range"][1].get<double>();
    }
    return binding;
}

}  // namespace

bool ParseManifest(const std::filesystem::path& path, Manifest& out,
                   std::string* error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        if (error) {
            *error = "cannot open file";
        }
        return false;
    }
    try {
        const json root = json::parse(in, nullptr, true, true);

        Manifest parsed;
        parsed.schema_version = root.value("schema_version", "");
        if (const auto meta = root.find("game_metadata");
            meta != root.end() && meta->is_object()) {
            parsed.game_metadata.game_id = meta->value("game_id", "");
            parsed.game_metadata.title = meta->value("title", "");
            parsed.game_metadata.executable = meta->value("executable", "");
            parsed.game_metadata.engine = meta->value("engine", "");
            parsed.game_metadata.graphics_api = meta->value("graphics_api", "");
        }

        // Spec §3.1 spells it "memory_Bindings" — accept both spellings.
        const json* bindings = nullptr;
        if (auto it = root.find("memory_Bindings"); it != root.end()) {
            bindings = &*it;
        } else if (auto it2 = root.find("memory_bindings"); it2 != root.end()) {
            bindings = &*it2;
        }
        if (bindings != nullptr && bindings->is_object()) {
            for (auto it = bindings->begin(); it != bindings->end(); ++it) {
                std::string parse_error;
                PointerBinding binding = ParseBinding(it.value(), &parse_error);
                if (!parse_error.empty()) {
                    if (error) {
                        *error = "binding '" + it.key() + "': " + parse_error;
                    }
                    return false;
                }
                parsed.memory_bindings[it.key()] = std::move(binding);
            }
        }

        if (auto it = root.find("states"); it != root.end() && it->is_array()) {
            for (const auto& s : *it) {
                State state;
                state.id = s.value("id", "");
                state.category = s.value("category", "");
                state.priority = s.value("priority", 0);
                state.condition = s.value("condition", "");
                state.haptics_mute = s.value("haptics_mute", false);
                if (auto lt = s.find("left_trigger"); lt != s.end()) {
                    state.left_trigger = ParseTrigger(*lt);
                }
                if (auto rt = s.find("right_trigger"); rt != s.end()) {
                    state.right_trigger = ParseTrigger(*rt);
                }
                if (auto hl = s.find("haptic_loop"); hl != s.end() &&
                                                      hl->is_object()) {
                    state.haptic_loop.present = true;
                    state.haptic_loop.file = hl->value("file", "");
                    state.haptic_loop.gain_binding =
                        hl->value("gain_binding", "");
                }
                parsed.states.push_back(std::move(state));
            }
        }

        if (auto it = root.find("audio_event_triggers");
            it != root.end() && it->is_array()) {
            for (const auto& t : *it) {
                AudioEventTrigger trigger;
                trigger.event_id = t.value("event_id", "");
                trigger.category = t.value("category", "");
                trigger.wwise_event_hash = t.value("wwise_event_hash", "");
                trigger.speaker_wav = t.value("speaker_wav", "");
                trigger.haptic_wav = t.value("haptic_wav", "");
                if (auto tp = t.find("trigger_pulse");
                    tp != t.end() && tp->is_object()) {
                    trigger.trigger_pulse.present = true;
                    trigger.trigger_pulse.target = tp->value("target", "both");
                    trigger.trigger_pulse.frequency_hz =
                        tp->value("frequency_hz", 0);
                    trigger.trigger_pulse.duration_ms =
                        tp->value("duration_ms", 0);
                    trigger.trigger_pulse.amplitude = tp->value("amplitude", 0);
                }
                parsed.audio_event_triggers.push_back(std::move(trigger));
            }
        }

        if (auto it = root.find("event_effects");
            it != root.end() && it->is_array()) {
            for (const auto& e : *it) {
                EventEffect effect;
                effect.id = e.value("id", "");
                if (effect.id.empty()) {
                    continue;  // fail-tolerant: skip unnamed effects
                }
                effect.present = true;
                if (auto rgb_node = e.find("lightbar_rgb");
                    rgb_node != e.end() && rgb_node->is_array() &&
                    rgb_node->size() == 3) {
                    effect.lightbar_rgb = {
                        (*rgb_node)[0].get<int>(), (*rgb_node)[1].get<int>(),
                        (*rgb_node)[2].get<int>()};
                }
                effect.motor_left = e.value("motor_left", 0);
                effect.motor_right = e.value("motor_right", 0);
                effect.trigger_amplitude = e.value("trigger_amplitude", 0);
                effect.trigger_frequency_hz =
                    e.value("trigger_frequency_hz", 0);
                effect.duration_ms = e.value("duration_ms", 300);
                if (auto tone = e.find("speaker_tone");
                    tone != e.end() && tone->is_object()) {
                    effect.speaker_tone.frequency_hz =
                        tone->value("frequency_hz", 0);
                    effect.speaker_tone.duration_ms =
                        tone->value("duration_ms", 120);
                    effect.speaker_tone.gain = tone->value("gain", 0.5);
                    effect.speaker_tone.second_hz =
                        tone->value("second_hz", 0);
                }
                parsed.event_effects.push_back(std::move(effect));
            }
        }

        if (auto it = root.find("lighting_rules");
            it != root.end() && it->is_object()) {
            parsed.lighting_rules.lightbar_mode =
                it->value("lightbar_mode", "health_and_adrenaline");
            parsed.lighting_rules.player_leds_binding =
                it->value("player_leds_binding", "");
            auto rgb = [](const json& node, std::array<int, 3> fallback) {
                if (!node.is_array() || node.size() != 3) {
                    return fallback;
                }
                return std::array<int, 3>{node[0].get<int>(), node[1].get<int>(),
                                          node[2].get<int>()};
            };
            parsed.lighting_rules.high_health_rgb =
                rgb(it->value("high_health_rgb", json::array()),
                    parsed.lighting_rules.high_health_rgb);
            parsed.lighting_rules.low_health_rgb =
                rgb(it->value("low_health_rgb", json::array()),
                    parsed.lighting_rules.low_health_rgb);
            parsed.lighting_rules.adrenaline_full_pulse_rgb = rgb(
                it->value("adrenaline_full_pulse_rgb", json::array()),
                parsed.lighting_rules.adrenaline_full_pulse_rgb);
        }

        // SF-17: optional panel description. Tolerant like the rest of
        // the parser: malformed entries are skipped, never fatal.
        if (auto it = root.find("ui"); it != root.end() && it->is_object()) {
            const auto entries = [](const json& node,
                                    std::map<std::string, UiEntry>& out_map) {
                if (!node.is_object()) {
                    return;
                }
                for (const auto& [id, value] : node.items()) {
                    UiEntry entry;
                    if (value.is_string()) {  // shorthand: "id": "Name"
                        entry.name = value.get<std::string>();
                    } else if (value.is_object()) {
                        entry.name = value.value("name", "");
                        entry.description = value.value("description", "");
                        entry.hidden = value.value("hidden", false);
                        entry.order = value.value("order", 0);
                    } else {
                        continue;
                    }
                    out_map[id] = std::move(entry);
                }
            };
            entries(it->value("states", json::object()), parsed.ui.states);
            entries(it->value("events", json::object()), parsed.ui.events);
            entries(it->value("categories", json::object()),
                    parsed.ui.categories);
            parsed.ui.guide_url = it->value("guide_url", "");
            parsed.ui.credits = it->value("credits", "");
            static const char* const kProfileKeys[] = {"", "weak", "medium",
                                                       "strong"};
            if (auto p = it->find("profiles");
                p != it->end() && p->is_object()) {
                for (int i = 1; i <= 3; ++i) {
                    if (auto q = p->find(kProfileKeys[i]);
                        q != p->end() && q->is_object()) {
                        parsed.ui.profiles[i].trigger_preset =
                            q->value("trigger_preset", -1);
                        parsed.ui.profiles[i].haptic_preset =
                            q->value("haptic_preset", -1);
                        parsed.ui.profiles[i].event_intensity =
                            q->value("event_intensity", -1);
                    }
                }
            }
        }

        out = std::move(parsed);
        return true;
    } catch (const json::exception& e) {
        if (error) {
            *error = e.what();
        }
        return false;
    }
}

namespace {

// SF-22: ids become ini keys (OverlaySettings) and travel in JSON lines to
// Studio, so they stay plain ASCII names. ASCII checks on purpose: the C
// locale functions would make this depend on the host settings.
bool IsPlainId(const std::string& id) {
    if (id.empty() || id.size() > 64) {
        return false;
    }
    for (const char c : id) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                        (c >= '0' && c <= '9') || c == '_' || c == '.' ||
                        c == '-';
        if (!ok) {
            return false;
        }
    }
    return true;
}

}  // namespace

std::vector<std::string> ValidateManifest(const Manifest& manifest) {
    std::vector<std::string> problems;
    const auto binding_exists = [&](const std::string& id) {
        return manifest.memory_bindings.find(id) !=
               manifest.memory_bindings.end();
    };
    for (const auto& effect : manifest.event_effects) {
        if (!effect.present) {
            continue;
        }
        if (effect.id.empty()) {
            problems.push_back("event_effect: empty id");
        } else if (!IsPlainId(effect.id)) {
            // The id is not echoed: it is exactly what must not be trusted.
            problems.push_back(
                "event_effect: id must use only A-Z a-z 0-9 _ . - (max 64)");
        }
        if (effect.duration_ms < 50 || effect.duration_ms > 5000) {
            problems.push_back("event_effect '" + effect.id +
                               "': duration_ms out of range (50..5000)");
        }
        for (int c : effect.lightbar_rgb) {
            if (c < 0 || c > 255) {
                problems.push_back("event_effect '" + effect.id +
                                   "': lightbar_rgb out of range (0..255)");
                break;
            }
        }
        if (effect.motor_left < 0 || effect.motor_left > 255 ||
            effect.motor_right < 0 || effect.motor_right > 255) {
            problems.push_back("event_effect '" + effect.id +
                               "': motor out of range (0..255)");
        }
        if (effect.trigger_amplitude < 0 || effect.trigger_amplitude > 8) {
            problems.push_back("event_effect '" + effect.id +
                               "': trigger_amplitude out of range (0..8)");
        }
    }
    for (const auto& state : manifest.states) {
        if (!state.id.empty() && !IsPlainId(state.id)) {
            problems.push_back(
                "state: id must use only A-Z a-z 0-9 _ . - (max 64)");
        }
        if (!state.category.empty() && !IsPlainId(state.category)) {
            problems.push_back(
                "state: category must use only A-Z a-z 0-9 _ . - (max 64)");
        }
        if (!state.left_trigger.scale_binding.empty() &&
            !binding_exists(state.left_trigger.scale_binding)) {
            problems.push_back("state '" + state.id +
                               "': left scale_binding '" +
                               state.left_trigger.scale_binding + "' missing");
        }
        if (!state.right_trigger.scale_binding.empty() &&
            !binding_exists(state.right_trigger.scale_binding)) {
            problems.push_back("state '" + state.id +
                               "': right scale_binding '" +
                               state.right_trigger.scale_binding + "' missing");
        }
        if (state.haptic_loop.present && !state.haptic_loop.file.empty()) {
            // SF-22: the runtime also refuses to leave the game directory;
            // this tells the MOD author before the file ever ships.
            const std::filesystem::path loop(state.haptic_loop.file);
            bool unsafe = loop.is_absolute() || loop.has_root_name() ||
                          loop.has_root_directory();
            for (const auto& part : loop) {
                if (part == "..") {
                    unsafe = true;
                }
            }
            if (unsafe) {
                problems.push_back(
                    "state: haptic_loop.file must be a relative path inside "
                    "the game directory");
            }
        }
        if (state.haptic_loop.present &&
            !state.haptic_loop.gain_binding.empty() &&
            !binding_exists(state.haptic_loop.gain_binding)) {
            problems.push_back("state '" + state.id +
                               "': haptic gain_binding '" +
                               state.haptic_loop.gain_binding + "' missing");
        }
    }
    if (!manifest.lighting_rules.player_leds_binding.empty() &&
        !binding_exists(manifest.lighting_rules.player_leds_binding)) {
        problems.push_back("lighting player_leds_binding '" +
                           manifest.lighting_rules.player_leds_binding +
                           "' missing");
    }
    // SF-17: a "ui" entry must describe something that exists (catches
    // typos that would silently leave an id unnamed in the panel).
    for (const auto& [id, entry] : manifest.ui.states) {
        const bool known = std::any_of(
            manifest.states.begin(), manifest.states.end(),
            [&](const State& state) { return state.id == id; });
        if (!known) {
            problems.push_back("ui.states '" + id + "': no such state");
        }
    }
    for (const auto& [id, entry] : manifest.ui.events) {
        const bool known = std::any_of(
            manifest.event_effects.begin(), manifest.event_effects.end(),
            [&](const EventEffect& effect) { return effect.id == id; });
        if (!known) {
            problems.push_back("ui.events '" + id + "': no such event_effect");
        }
    }
    for (const auto& [id, entry] : manifest.ui.categories) {
        const bool known = std::any_of(
            manifest.states.begin(), manifest.states.end(),
            [&](const State& state) { return state.category == id; });
        if (!known) {
            problems.push_back("ui.categories '" + id +
                               "': no state uses this category");
        }
    }
    return problems;
}

}  // namespace senseforge

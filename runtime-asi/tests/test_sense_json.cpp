// test_sense_json.cpp — {game}.sense.json manifest parser (SF-3).
// Fixture: trimmed sample of SENSEFORGE_SPEC.md §3.1 (schema, both trigger
// spellings of the bindings key covered by the parser).

#include "test_common.hpp"

#include <filesystem>
#include <fstream>

#include "sense_json.hpp"

namespace {

std::filesystem::path WriteFixture(const std::string& content) {
    const auto path =
        std::filesystem::temp_directory_path() / "sf_test_manifest.sense.json";
    std::ofstream out(path, std::ios::trunc);
    out << content;
    return path;
}

const char* kSampleManifest = R"JSON({
  "schema_version": "1.0.0",
  "game_metadata": {
    "game_id": "acodyssey",
    "title": "Assassin's Creed Odyssey",
    "executable": "ACOdyssey.exe",
    "engine": "AnvilNext 2.0",
    "graphics_api": "DX11"
  },
  "memory_Bindings": {
    "is_menu_or_map_open": {
      "module": "ACOdyssey.exe",
      "base_rva": "0x04A82F10",
      "pointer_offsets": ["0x18", "0x40", "0x0C"],
      "type": "uint8",
      "expected_range": [0, 1]
    },
    "bow_draw_tension": {
      "module": "ACOdyssey.exe",
      "base_rva": "0x04B19C40",
      "pointer_offsets": ["0x20", "0x14C"],
      "type": "float",
      "expected_range": [0.0, 1.0]
    },
    "adrenaline_segments": {
      "module": "ACOdyssey.exe",
      "base_rva": "0x049F1020",
      "pointer_offsets": ["0x10", "0x90", "0x14"],
      "type": "uint8",
      "expected_range": [0, 5]
    }
  },
  "states": [
    {
      "id": "menu_override",
      "category": "ui",
      "priority": 100,
      "condition": "is_menu_or_map_open == 1",
      "left_trigger": { "mode": "off" },
      "right_trigger": { "mode": "off" },
      "haptics_mute": true
    },
    {
      "id": "bow_aim_and_draw",
      "category": "bow_and_ranged",
      "priority": 80,
      "condition": "is_aiming_bow == 1",
      "left_trigger": { "mode": "resistance", "start_position": 1, "force": 3 },
      "right_trigger": {
        "mode": "bow_elastic_scaled",
        "start_position": 1,
        "end_position": 8,
        "base_force": 3,
        "max_force": 8,
        "scale_binding": "bow_draw_tension",
        "full_draw_vibration_hz": 38,
        "full_draw_threshold": 0.95
      },
      "haptic_loop": {
        "file": "haptics/bow_string_creak_ch34.wav",
        "gain_binding": "bow_draw_tension"
      }
    },
    {
      "id": "melee_combat_stance",
      "category": "melee_combat",
      "priority": 50,
      "condition": "is_menu_or_map_open == 0 && is_aiming_bow == 0",
      "left_trigger": { "mode": "rigid_section", "start_position": 2, "end_position": 6, "force": 5 },
      "right_trigger": { "mode": "weapon_snap", "start_position": 2, "end_position": 5, "force": 6 }
    }
  ],
  "audio_event_triggers": [
    {
      "event_id": "parry_perfect_spark",
      "category": "melee_combat",
      "wwise_event_hash": "0x8F41A20C",
      "speaker_wav": "haptics/parry_spark_speaker_ch12.wav",
      "haptic_wav": "haptics/parry_impact_sub_ch34.wav",
      "trigger_pulse": { "target": "both", "frequency_hz": 45, "duration_ms": 120, "amplitude": 7 }
    },
    {
      "event_id": "phobos_whistle",
      "category": "mount_and_ship",
      "wwise_event_hash": "0x3C19E044",
      "speaker_wav": "haptics/phobos_whistle_speaker_ch12.wav",
      "haptic_wav": "haptics/hoof_approaching_ch34.wav"
    }
  ],
  "lighting_rules": {
    "lightbar_mode": "health_and_adrenaline",
    "high_health_rgb": [20, 210, 80],
    "low_health_rgb": [230, 25, 25],
    "adrenaline_full_pulse_rgb": [255, 195, 0],
    "player_leds_binding": "adrenaline_segments"
  }
})JSON";

}  // namespace

int main() {
    const auto path = WriteFixture(kSampleManifest);

    senseforge::Manifest manifest;
    std::string error;
    assert(senseforge::ParseManifest(path, manifest, &error));
    assert(error.empty());

    // Metadata + bindings (spec spells the key "memory_Bindings").
    assert(manifest.schema_version == "1.0.0");
    assert(manifest.game_metadata.executable == "ACOdyssey.exe");
    assert(manifest.memory_bindings.size() == 3);

    const auto& menu = manifest.memory_bindings.at("is_menu_or_map_open");
    assert(menu.base_rva == 0x04A82F10);
    assert(menu.pointer_offsets.size() == 3);
    assert(menu.pointer_offsets[2] == 0x0C);
    assert(menu.type == senseforge::BindingType::UInt8);
    assert(menu.has_expected_range && menu.range_min == 0.0 &&
           menu.range_max == 1.0);

    const auto& tension = manifest.memory_bindings.at("bow_draw_tension");
    assert(tension.type == senseforge::BindingType::Float32);

    // States + adaptive trigger modes.
    assert(manifest.states.size() == 3);
    const auto& menu_state = manifest.states[0];
    assert(menu_state.id == "menu_override");
    assert(menu_state.priority == 100);
    assert(menu_state.haptics_mute);
    assert(menu_state.left_trigger.mode == senseforge::TriggerMode::Off);

    const auto& bow = manifest.states[1];
    assert(bow.right_trigger.mode ==
           senseforge::TriggerMode::BowElasticScaled);
    assert(bow.right_trigger.scale_binding == "bow_draw_tension");
    assert(bow.right_trigger.full_draw_vibration_hz == 38);
    assert(bow.right_trigger.full_draw_threshold == 0.95);
    assert(bow.left_trigger.mode == senseforge::TriggerMode::Resistance);
    assert(bow.haptic_loop.present);
    assert(bow.haptic_loop.gain_binding == "bow_draw_tension");

    assert(manifest.states[2].left_trigger.mode ==
           senseforge::TriggerMode::RigidSection);
    assert(manifest.states[2].right_trigger.mode ==
           senseforge::TriggerMode::WeaponSnap);

    // Audio event triggers (Wwise hashes pass through verbatim).
    assert(manifest.audio_event_triggers.size() == 2);
    assert(manifest.audio_event_triggers[0].wwise_event_hash ==
           "0x8F41A20C");
    assert(manifest.audio_event_triggers[0].trigger_pulse.present);
    assert(manifest.audio_event_triggers[0].trigger_pulse.frequency_hz == 45);
    assert(manifest.audio_event_triggers[0].trigger_pulse.duration_ms == 120);
    assert(manifest.audio_event_triggers[0].trigger_pulse.amplitude == 7);
    assert(!manifest.audio_event_triggers[1].trigger_pulse.present);

    // Lighting rules.
    assert(manifest.lighting_rules.high_health_rgb[1] == 210);
    assert(manifest.lighting_rules.player_leds_binding ==
           "adrenaline_segments");

    // Cross-references resolve (scale/gain/LEDs bindings exist).
    assert(senseforge::ValidateManifest(manifest).empty());

    // Hot-reload safety: a corrupted file must NOT touch the previous value.
    const auto bad = WriteFixture("{ not json !!!");
    senseforge::Manifest fresh;
    std::string bad_error;
    assert(!senseforge::ParseManifest(bad, fresh, &bad_error));
    assert(!bad_error.empty());
    assert(fresh.memory_bindings.empty());

    // ---- SF-17: optional "ui" section -----------------------------------
    {
        const auto ui_path = WriteFixture(R"({
          "states": [{"id": "combat", "category": "combat", "priority": 1,
                      "condition": "x == 1"},
                     {"id": "explore", "category": "movement", "priority": 0,
                      "condition": "x == 0"}],
          "event_effects": [{"id": "parry", "duration_ms": 300}],
          "ui": {
            "guide_url": "https://example.com/guide",
            "credits": "MOD de teste",
            "states": {
              "combat": {"name": "Combate", "description": "R2 travado",
                         "order": 2},
              "explore": {"name": "Exploracao", "hidden": true},
              "ghost": "Nao existe"
            },
            "events": {"parry": "Parry"},
            "categories": {"combat": {"name": "Luta"}},
            "profiles": {"weak": {"trigger_preset": 2, "event_intensity": 40}}
          }
        })");
        senseforge::Manifest ui_manifest;
        std::string ui_error;
        assert(senseforge::ParseManifest(ui_path, ui_manifest, &ui_error));
        const auto& ui = ui_manifest.ui;
        assert(ui.guide_url == "https://example.com/guide");
        assert(ui.credits == "MOD de teste");
        assert(ui.states.at("combat").name == "Combate");
        assert(ui.states.at("combat").description == "R2 travado");
        assert(ui.states.at("combat").order == 2);
        assert(ui.states.at("explore").hidden);
        assert(ui.events.at("parry").name == "Parry");  // string shorthand
        assert(ui.categories.at("combat").name == "Luta");
        assert(ui.profiles[1].trigger_preset == 2);
        assert(ui.profiles[1].event_intensity == 40);
        assert(ui.profiles[1].haptic_preset == -1);  // not declared
        assert(ui.profiles[3].trigger_preset == -1);
        // A ui entry for an unknown id is reported, nothing else is.
        const auto problems = senseforge::ValidateManifest(ui_manifest);
        assert(problems.size() == 1);
        assert(problems[0].find("ghost") != std::string::npos);

        // No "ui" section: everything empty, ids are shown raw.
        const auto plain_path = WriteFixture(
            R"({"states": [], "event_effects": [{"id": "parry"}]})");
        senseforge::Manifest plain;
        assert(senseforge::ParseManifest(plain_path, plain, &ui_error));
        assert(plain.ui.states.empty() && plain.ui.guide_url.empty());

        // Wrong types inside "ui" are an error, not a crash.
        const auto bad_ui = WriteFixture(
            R"({"ui": {"profiles": {"weak": {"trigger_preset": "x"}}}})");
        senseforge::Manifest bad_manifest;
        assert(!senseforge::ParseManifest(bad_ui, bad_manifest, &ui_error));
        std::filesystem::remove(ui_path);
        std::filesystem::remove(plain_path);
        std::filesystem::remove(bad_ui);
    }

    std::filesystem::remove(path);
    return 0;
}

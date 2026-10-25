// manifest_check.cpp — SF-10: manifest validation tool.
//
// Runs the REAL runtime parser (sense_json.cpp ParseManifest) plus
// ValidateManifest over a .sense.json file and prints a summary:
// bindings, states, triggers and any referential problems. Exit code 0
// only when the manifest parses clean AND validation finds no issues.
//
// Usage: sf_manifest_check <path\to\game.sense.json>

#include <cstdio>
#include <filesystem>
#include <string>

#include "sense_json.hpp"

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: %s <manifest.sense.json>\n", argv[0]);
        return 2;
    }
    const std::filesystem::path path = argv[1];
    senseforge::Manifest manifest;
    std::string error;
    if (!senseforge::ParseManifest(path, manifest, &error)) {
        std::fprintf(stderr, "PARSE FAILED: %s\n", error.c_str());
        return 1;
    }
    std::printf(
        "schema=%s game=%s exe=%s\n",
        manifest.schema_version.c_str(),
        manifest.game_metadata.game_id.c_str(),
        manifest.game_metadata.executable.c_str());
    std::printf("bindings: %zu\n", manifest.memory_bindings.size());
    for (const auto& [name, binding] : manifest.memory_bindings) {
        std::printf("  %-22s rva=%#llx offsets=%zu type=%d\n",
                    name.c_str(),
                    static_cast<unsigned long long>(binding.base_rva),
                    binding.pointer_offsets.size(),
                    static_cast<int>(binding.type));
    }
    std::printf("states: %zu\n", manifest.states.size());
    for (const auto& state : manifest.states) {
        std::printf("  %-22s prio=%3d cat=%-9s cond=\"%s\" mute=%d\n",
                    state.id.c_str(), state.priority,
                    state.category.c_str(), state.condition.c_str(),
                    static_cast<int>(state.haptics_mute));
    }
    std::printf("audio_event_triggers: %zu\n",
                manifest.audio_event_triggers.size());
    for (const auto& trigger : manifest.audio_event_triggers) {
        std::printf("  %-24s hash=%s haptic=%s speaker=%s\n",
                    trigger.event_id.c_str(),
                    trigger.wwise_event_hash.c_str(),
                    trigger.haptic_wav.c_str(),
                    trigger.speaker_wav.c_str());
    }
    const auto problems = senseforge::ValidateManifest(manifest);
    if (!problems.empty()) {
        std::fprintf(stderr, "VALIDATION PROBLEMS (%zu):\n", problems.size());
        for (const auto& problem : problems) {
            std::fprintf(stderr, "  - %s\n", problem.c_str());
        }
        return 1;
    }
    std::printf("VALIDATION: clean\n");
    return 0;
}

// runtime_hub.hpp — SF-14: the bridge between the worker thread
// (dllmain.cpp) and the ReShade panel (reshade_panel.cpp, render thread).
//
// The worker publishes a status snapshot every ~200ms; the panel copies it
// under the same mutex. Requests go the other way (test effect / test
// flash) and are consumed by the worker on its next tick. Settings never
// travel through here: the panel writes senseforge_user.ini and the
// worker's hot-reload watcher applies it (<10ms), exactly like Studio and
// the Sony overlay.

#pragma once

#include <cstdint>
#include <deque>
#include <filesystem>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "sense_json.hpp"  // ManifestUi (SF-17)

namespace senseforge {

// SF-17 creator tools: one gameplay event as the worker saw it.
struct EventLogEntry {
    std::uint64_t ms = 0;     // GetTickCount64() at arrival
    std::string kind;         // "flash" | "state" | "test"
    std::string id;
    double value = 0.0;       // sf_state value / flash scale
    bool applied = false;     // felt on the controller
    std::string note;         // why not applied ("desligado", "sem efeito")
};

struct RuntimeStatus {
    bool worker_ready = false;     // warmup gate passed, subsystems up
    bool enabled = true;           // [SenseForge] enabled (master switch)
    std::string transport = "none";  // direct_hid | udp_dsx | none
    bool hid_connected = false;
    std::string active_state = "boot";
    int bindings_live = 0;
    int bindings_dead = 0;
    std::string last_flash_id;
    std::uint64_t last_flash_ms = 0;  // GetTickCount64() at arrival
    std::uint64_t flash_count = 0;
    bool audio_ok = false;
    std::string audio_device;
    std::string audio_error;
    std::filesystem::path ini_path;
    std::filesystem::path manifest_path;
    bool manifest_ok = false;
    std::string manifest_error;
    std::string game_title;
    // Manifest inventory for the per-effect section (manifest order).
    std::vector<std::string> state_ids;
    std::vector<std::string> state_categories;  // parallel to state_ids
    std::vector<std::string> event_ids;
    // SF-17: the MOD's own panel description and identity.
    ManifestUi ui;
    std::string mod_version;
    std::vector<std::string> manifest_problems;  // ValidateManifest
    // SF-17 creator tools (filled every publish; cheap).
    std::deque<EventLogEntry> recent_events;  // newest last, max 50
    std::map<std::string, double> binding_values;
    int active_priority = 0;
};

class RuntimeHub {
public:
    static RuntimeHub& Instance();

    // Worker side.
    void Publish(const RuntimeStatus& status);
    bool ConsumeTestEffect();
    std::optional<std::string> ConsumeTestFlash();
    bool ConsumeReloadManifest();

    // Panel side.
    RuntimeStatus Snapshot() const;
    void RequestTestEffect();
    void RequestTestFlash(const std::string& event_id);
    void RequestReloadManifest();

private:
    RuntimeHub() = default;

    mutable std::mutex mutex_;
    RuntimeStatus status_;
    bool test_effect_ = false;
    std::optional<std::string> test_flash_;
    bool reload_manifest_ = false;
};

}  // namespace senseforge

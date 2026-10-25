// event_bridge.hpp — SF-12b: UDP gameplay-event listener.
//
// UE4SS Lua mods (E33: Clair Obscur Expedition 33) fire gameplay events
// at this non-blocking UDP socket on 127.0.0.1:<port> (ini [Events]):
//   {"type":"sf_flash","id":"parry","scale":1.0}   -> transient effect;
//       the worker resolves `id` against the manifest's event_effects and
//       overlays lightbar/rumble/trigger-pulse for duration_ms (Q9).
//   {"type":"sf_state","id":"is_aiming","value":1.0} -> persistent value
//       merged into the state-engine binding map (conditions may reference
//       the id directly).
//   anything else is ignored silently — the game never sees a parse error.
//
// Threading: Poll()/ClearFlashes() run on the worker thread only. The
// socket is non-blocking; Poll drains at most kMaxDatagrams per call so a
// flooding sender can never stall the 5ms tick.

#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "state_engine.hpp"  // BindingValues

namespace senseforge {

class EventBridge {
public:
    struct Settings {
        std::uint16_t port = 6970;
    };

    explicit EventBridge(Settings settings);
    ~EventBridge();

    EventBridge(const EventBridge&) = delete;
    EventBridge& operator=(const EventBridge&) = delete;

    // Binds 127.0.0.1:port (non-blocking). Returns false when winsock or
    // the bind fails — the runtime keeps running without event feedback.
    bool Start();

    // ---- File event source (SF-12b) ---------------------------------------
    // The game's UE4SS Lua sandbox (E33 build) ships without LuaSocket, so
    // the bridge also tails an NDJSON event file next to the game exe
    // (ini [Events] file=...): one JSON object per line, same schema as
    // the UDP datagrams. Tailing starts at the file's current end —
    // events from a previous run are stale by definition.
    void StartFile(const std::filesystem::path& path);
    void PollFile();

    // Drains the socket (bounded). Parses datagrams, accumulates flashes
    // and updates persistent values.
    void Poll();

    struct Flash {
        std::string id;
        double scale = 1.0;
    };

    // Flashes accumulated since the last ClearFlashes().
    const std::vector<Flash>& pending_flashes() const {
        return pending_flashes_;
    }
    void ClearFlashes() { pending_flashes_.clear(); }

    // Persistent values ("sf_state" datagrams) — id -> value.
    const BindingValues& values() const { return values_; }

    bool started() const { return socket_ != nullptr; }

private:
    void HandleDatagram(const char* data, int length);

    Settings settings_;
    void* socket_ = nullptr;  // SOCKET, kept opaque (winsock leak safety)
    std::vector<Flash> pending_flashes_;
    BindingValues values_;
    bool wsa_started_ = false;
    // File tail source.
    std::filesystem::path file_path_;
    std::uint64_t file_offset_ = 0;
};

}  // namespace senseforge

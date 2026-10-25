// hot_reload.hpp — <10ms polling file watcher (SF-3).
//
// SENSEFORGE_SPEC.md §2.1: the runtime watches {game}.sense.json and
// senseforge_user.ini and reloads the state tree instantly (<10ms) without
// interrupting the game. The worker thread calls Poll() every ~5ms; the
// check is a stat (last-write-time + size) so the cost per tick is
// negligible and no extra threads or OS callbacks are involved.

#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <vector>

namespace senseforge {

class HotReloadWatcher {
public:
    using Callback = std::function<void(const std::filesystem::path&)>;

    void Register(std::filesystem::path path, Callback callback);

    // Fires callbacks for every file changed since the previous poll.
    // Returns how many callbacks fired.
    std::size_t Poll();

private:
    struct Entry {
        std::filesystem::path path;
        Callback callback;
        bool known = false;
        std::uintmax_t size = 0;
        std::filesystem::file_time_type modified{};
    };

    std::vector<Entry> entries_;
};

}  // namespace senseforge

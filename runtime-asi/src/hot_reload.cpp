// hot_reload.cpp — polling watcher implementation (SF-3).

#include "hot_reload.hpp"

namespace senseforge {

void HotReloadWatcher::Register(std::filesystem::path path,
                                 Callback callback) {
    entries_.push_back(Entry{std::move(path), std::move(callback), false, 0,
                             std::filesystem::file_time_type{}});
}

std::size_t HotReloadWatcher::Poll() {
    std::size_t fired = 0;
    for (Entry& entry : entries_) {
        std::error_code ec;
        const std::filesystem::path& path = entry.path;

        if (!std::filesystem::exists(path, ec) || ec) {
            continue;
        }

        const auto modified = std::filesystem::last_write_time(path, ec);
        if (ec) {
            continue;
        }
        const auto size = std::filesystem::file_size(path, ec);
        if (ec) {
            continue;
        }

        const bool changed =
            entry.known &&
            (modified != entry.modified || size != entry.size);
        entry.known = true;
        entry.modified = modified;
        entry.size = size;

        if (changed) {
            ++fired;
            entry.callback(path);  // hot-reload the affected document
        }
    }
    return fired;
}

}  // namespace senseforge

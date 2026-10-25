// test_hot_reload.cpp — <10ms polling watcher (SF-3).
// Writes a file, registers it, then rewrites with fresh content and asserts
// the callback fires with the updated bytes readable.

#include "test_common.hpp"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>

#include "hot_reload.hpp"

int main() {
    const auto path =
        std::filesystem::temp_directory_path() / "sf_test_hotreload.json";
    {
        std::ofstream out(path, std::ios::trunc);
        out << "{\"v\":1}";
    }

    senseforge::HotReloadWatcher watcher;
    std::atomic<int> fired{0};
    std::string seen;

    watcher.Register(path, [&](const std::filesystem::path& p) {
        ++fired;
        std::ifstream in(p);
        std::string content((std::istreambuf_iterator<char>(in)),
                            std::istreambuf_iterator<char>());
        seen = content;
    });

    // First poll only records the baseline — must NOT fire.
    assert(watcher.Poll() == 0);

    // No modification: still silent.
    assert(watcher.Poll() == 0);

    // Rewrite ( Sleep > NTFS timestamp granularity to force a new mtime ).
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    {
        std::ofstream out(path, std::ios::trunc);
        out << "{\"v\":2}";
    }

    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(500);
    while (fired == 0 && std::chrono::steady_clock::now() < deadline) {
        watcher.Poll();
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    assert(fired >= 1);
    assert(seen == "{\"v\":2}");

    // Second poll after the fire: quiescent again (no duplicate callbacks).
    assert(watcher.Poll() == 0);

    std::filesystem::remove(path);
    return 0;
}

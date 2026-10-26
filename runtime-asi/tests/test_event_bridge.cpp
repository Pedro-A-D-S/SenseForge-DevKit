// test_event_bridge.cpp — SF-12b: UDP gameplay-event bridge (UE4SS Lua
// side). Real loopback datagrams: flash parsing, persistent sf_state
// values, scale clamping and garbage tolerance.

#include "test_common.hpp"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <cstring>
#include <cmath>
#include <filesystem>
#include <fstream>

#include "event_bridge.hpp"

using namespace senseforge;

namespace {

constexpr std::uint16_t kTestPort = 27970;

bool SendUdp(const char* payload) {
    const SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET) {
        return false;
    }
    sockaddr_in to{};
    to.sin_family = AF_INET;
    to.sin_port = htons(kTestPort);
    to.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    const int sent = sendto(s, payload,
                            static_cast<int>(std::strlen(payload)), 0,
                            reinterpret_cast<sockaddr*>(&to), sizeof(to));
    closesocket(s);
    return sent > 0;
}

}  // namespace

int main() {
    WSADATA data{};
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
        return 1;  // no winsock — can't test (treated as environment skip)
    }

    EventBridge bridge(EventBridge::Settings{kTestPort});
    if (!bridge.Start()) {
        WSACleanup();
        return 1;  // loopback bind refused — environment issue, fail loudly
    }
    assert(bridge.started());

    // Empty poll is a cheap no-op.
    bridge.Poll();
    assert(bridge.pending_flashes().empty());

    // sf_flash with explicit scale.
    assert(SendUdp(R"({"type":"sf_flash","id":"parry","scale":0.9})"));
    Sleep(30);
    bridge.Poll();
    assert(bridge.pending_flashes().size() == 1);
    assert(bridge.pending_flashes()[0].id == "parry");
    assert(bridge.pending_flashes()[0].scale == 0.9);
    bridge.ClearFlashes();
    assert(bridge.pending_flashes().empty());

    // sf_flash without scale defaults to 1.0.
    assert(SendUdp(R"({"type":"sf_flash","id":"gradient"})"));
    Sleep(30);
    bridge.Poll();
    assert(bridge.pending_flashes().size() == 1);
    assert(bridge.pending_flashes()[0].id == "gradient");
    assert(bridge.pending_flashes()[0].scale == 1.0);
    bridge.ClearFlashes();

    // sf_state persistent values.
    assert(SendUdp(
        R"({"type":"sf_state","id":"is_aiming","value":1.0})"));
    assert(SendUdp(
        R"({"type":"sf_state","id":"tension","value":0.42})"));
    Sleep(30);
    bridge.Poll();
    assert(bridge.values().at("is_aiming") == 1.0);
    assert(bridge.values().at("tension") == 0.42);

    // Garbage and unknown datagrams never throw or leak into state.
    assert(SendUdp("not json at all"));
    assert(SendUdp(R"({"type":"sf_flash"})"));  // no id
    assert(SendUdp(R"({"type":"banana","id":"x"})"));
    assert(SendUdp(R"([1,2,3])"));  // not an object
    Sleep(30);
    bridge.Poll();
    assert(bridge.pending_flashes().empty());
    assert(bridge.values().size() == 2);

    // Scale clamps to [0, 1].
    assert(SendUdp(R"({"type":"sf_flash","id":"parry","scale":7.0})"));
    Sleep(30);
    bridge.Poll();
    assert(bridge.pending_flashes().size() == 1);
    assert(bridge.pending_flashes()[0].scale == 1.0);
    bridge.ClearFlashes();

    // ---- File source (SF-12b): UE4SS Lua NDJSON tail ----------------------
    {
        const auto path = std::filesystem::temp_directory_path() /
                           "sf_event_bridge_test.ndjson";
        std::error_code ec;
        std::filesystem::remove(path, ec);

        EventBridge file_bridge(EventBridge::Settings{27971});
        file_bridge.StartFile(path);
        // Missing file: PollFile is a no-op, never a failure.
        file_bridge.PollFile();
        assert(file_bridge.pending_flashes().empty());

        {
            std::ofstream out(path, std::ios::app);
            out << R"({"type":"sf_flash","id":"parry"})" << "\n";
            out << R"({"type":"sf_state","id":"is_aiming","value":1.0})" << "\n";
        }
        file_bridge.PollFile();
        assert(file_bridge.pending_flashes().size() == 1);
        assert(file_bridge.pending_flashes()[0].id == "parry");
        assert(file_bridge.values().at("is_aiming") == 1.0);
        file_bridge.ClearFlashes();

        // Stale-run rule: a NEW bridge starts at the file's current end —
        // pre-existing lines never replay.
        EventBridge stale(EventBridge::Settings{27972});
        stale.StartFile(path);
        stale.PollFile();
        assert(stale.pending_flashes().empty());

        {
            std::ofstream out(path, std::ios::app);
            out << R"({"type":"sf_flash","id":"dodge","scale":0.5})" << "\n";
        }
        stale.PollFile();
        assert(stale.pending_flashes().size() == 1);
        assert(stale.pending_flashes()[0].id == "dodge");
        assert(std::abs(stale.pending_flashes()[0].scale - 0.5) < 1e-9);
        stale.ClearFlashes();

        // SF-22: a line longer than the cap is dropped up to its newline
        // and the event after it still arrives (no unbounded line buffer).
        {
            std::ofstream out(path, std::ios::app);
            out << R"({"type":"sf_flash","id":")" << std::string(100000, 'a')
                << R"(","scale":1.0})" << "\n"
                << R"({"type":"sf_flash","id":"after_big","scale":1.0})" << "\n";
        }
        stale.PollFile();
        assert(stale.pending_flashes().size() == 1);
        assert(stale.pending_flashes()[0].id == "after_big");
        stale.ClearFlashes();

        // SF-22: one poll reads at most 1 MiB; the rest is picked up by the
        // next polls, and a line that crosses the cap is resumed whole.
        {
            std::ofstream out(path, std::ios::app);
            for (int i = 0; i < 40000; ++i) {
                out << R"({"type":"sf_state","id":"filler","value":1.0})" << "\n";
            }
            out << R"({"type":"sf_flash","id":"last_one","scale":1.0})" << "\n";
        }
        bool seen_last = false;
        for (int polls = 0; polls < 8 && !seen_last; ++polls) {
            stale.PollFile();
            for (const auto& flash : stale.pending_flashes()) {
                seen_last = seen_last || flash.id == "last_one";
            }
            stale.ClearFlashes();
        }
        assert(seen_last);

        // SF-22: ceilings on everything the producer chooses. New state
        // keys stop at 256 (known keys keep updating), ids over 64 chars are
        // ignored, and a flash burst cannot grow the queue without bound.
        {
            std::ofstream out(path, std::ios::app);
            for (int i = 0; i < 400; ++i) {
                out << R"({"type":"sf_state","id":"key_)" << i
                    << R"(","value":1.0})" << "\n";
            }
            out << R"({"type":"sf_state","id":"filler","value":7.0})" << "\n";
            out << R"({"type":"sf_state","id":")" << std::string(65, 'x')
                << R"(","value":1.0})" << "\n";
            for (int i = 0; i < 1000; ++i) {
                out << R"({"type":"sf_flash","id":"burst","scale":1.0})" << "\n";
            }
        }
        for (int polls = 0; polls < 4; ++polls) {
            stale.PollFile();
        }
        assert(stale.values().size() <= 256);
        assert(stale.values().count("key_399") == 0);       // over the cap
        assert(stale.values().at("filler") == 7.0);          // known key updates
        assert(stale.values().count(std::string(65, 'x')) == 0);
        assert(stale.pending_flashes().size() <= 256);
        stale.ClearFlashes();

        std::filesystem::remove(path, ec);
    }

    WSACleanup();
    return 0;
}

// event_bridge.cpp — SF-12b: UDP gameplay-event listener (UE4SS Lua side).
#include "event_bridge.hpp"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstring>

#include "sf_debug_logger.hpp"

namespace senseforge {
namespace {

constexpr int kMaxDatagramsPerPoll = 32;
constexpr int kMaxDatagramBytes = 512;
// SF-22: the Lua side (or anything that can reach the UDP port / the NDJSON
// file) chooses ids and keys, so every container they feed has a ceiling.
constexpr std::size_t kMaxIdLength = 64;
constexpr std::size_t kMaxStateKeys = 256;
constexpr std::size_t kMaxPendingFlashes = 256;
constexpr uintptr_t kInvalidSocket = static_cast<uintptr_t>(~0);

}  // namespace

EventBridge::EventBridge(Settings settings) : settings_(settings) {}

EventBridge::~EventBridge() {
    if (socket_ != nullptr &&
        reinterpret_cast<uintptr_t>(socket_) != kInvalidSocket) {
        closesocket(static_cast<SOCKET>(
            reinterpret_cast<uintptr_t>(socket_)));
    }
    if (wsa_started_) {
        // Refcounted: only actually tears winsock down when the last user
        // (e.g. the dualsensitive UDP transport) also releases it.
        WSACleanup();
    }
}

bool EventBridge::Start() {
    WSADATA data{};
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
        senseforge::DebugLog("event bridge: WSAStartup failed");
        return false;
    }
    wsa_started_ = true;

    const SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET) {
        senseforge::DebugLog("event bridge: socket failed err=%d",
                             WSAGetLastError());
        return false;
    }

    // Non-blocking: the worker polls — a missing sender must never stall
    // the 5ms tick.
    u_long non_blocking = 1;
    ioctlsocket(sock, FIONBIO, &non_blocking);

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(settings_.port);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(sock, reinterpret_cast<sockaddr*>(&address),
             sizeof(address)) == SOCKET_ERROR) {
        senseforge::DebugLog("event bridge: bind 127.0.0.1:%u failed err=%d",
                             settings_.port, WSAGetLastError());
        closesocket(sock);
        return false;
    }

    socket_ = reinterpret_cast<void*>(static_cast<uintptr_t>(sock));
    senseforge::DebugLog("event bridge: listening on 127.0.0.1:%u",
                         settings_.port);
    return true;
}

void EventBridge::Poll() {
    if (socket_ == nullptr ||
        reinterpret_cast<uintptr_t>(socket_) == kInvalidSocket) {
        return;
    }
    const SOCKET sock =
        static_cast<SOCKET>(reinterpret_cast<uintptr_t>(socket_));

    char buffer[kMaxDatagramBytes];
    for (int drained = 0; drained < kMaxDatagramsPerPoll; ++drained) {
        sockaddr_in from{};
        int from_len = sizeof(from);
        const int received = recvfrom(sock, buffer, sizeof(buffer), 0,
                                       reinterpret_cast<sockaddr*>(&from),
                                       &from_len);
        if (received <= 0) {
            break;  // WSAEWOULDBLOCK (empty) or transient error — done
        }
        HandleDatagram(buffer, received);
    }
}

void EventBridge::StartFile(const std::filesystem::path& path) {
    file_path_ = path;
    // Start at the current end: anything already in the file belongs to a
    // previous run and is stale. A missing file is fine — the Lua mod
    // creates it on the first event, and PollFile picks it up then.
    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    file_offset_ = ec ? 0 : static_cast<std::uint64_t>(size);
    senseforge::DebugLog("event bridge: tailing file %s (start offset %llu)",
                        path.string().c_str(),
                        static_cast<unsigned long long>(file_offset_));
}

void EventBridge::PollFile() {
    if (file_path_.empty()) {
        return;
    }
    WIN32_FILE_ATTRIBUTE_DATA info{};
    if (GetFileAttributesExW(file_path_.c_str(), GetFileExInfoStandard,
                             &info) == FALSE) {
        return;  // not created yet (or transiently unreadable) — try later
    }
    const std::uint64_t size =
        (static_cast<std::uint64_t>(info.nFileSizeHigh) << 32) |
        static_cast<std::uint64_t>(info.nFileSizeLow);
    if (size < file_offset_) {
        file_offset_ = 0;  // file was recreated (log rotation) — restart
    }
    if (size == file_offset_) {
        return;  // nothing new
    }

    const HANDLE file =
        CreateFileW(file_path_.c_str(), GENERIC_READ,
                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                    nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return;  // writer holds it in a mode we can't read — retry later
    }
    LARGE_INTEGER start{};
    start.QuadPart = static_cast<LONGLONG>(file_offset_);
    if (SetFilePointerEx(file, start, nullptr, FILE_BEGIN) != FALSE) {
        // SF-22: the writer is a file in the game folder, so neither a
        // line nor one poll may grow without bound. Real events are tens
        // of bytes; an oversized line is dropped up to its newline.
        constexpr std::size_t kMaxLineBytes = 4096;
        constexpr std::uint64_t kMaxPollBytes = 1u << 20;
        char buffer[4096];
        DWORD read = 0;
        std::string line;
        line.reserve(256);
        bool oversized = false;
        std::uint64_t consumed = 0;
        std::uint64_t line_start = file_offset_;  // where the open line began
        while (consumed < kMaxPollBytes &&
               ReadFile(file, buffer, sizeof(buffer), &read, nullptr) != FALSE &&
               read > 0) {
            const std::uint64_t chunk_start = file_offset_;
            file_offset_ += read;
            consumed += read;
            for (DWORD i = 0; i < read; ++i) {
                const char c = buffer[i];
                if (c == '\n') {
                    if (!oversized && !line.empty()) {
                        HandleDatagram(line.data(), static_cast<int>(line.size()));
                    }
                    line.clear();
                    oversized = false;
                    line_start = chunk_start + i + 1;
                } else if (c != '\r' && !oversized) {
                    if (line.size() >= kMaxLineBytes) {
                        oversized = true;
                        line.clear();
                    } else {
                        line.push_back(c);
                    }
                }
            }
        }
        // Stopped at the per-poll cap in the middle of a line: resume at its
        // start next time instead of splitting it between two polls.
        if (consumed >= kMaxPollBytes && !oversized && !line.empty()) {
            file_offset_ = line_start;
        }
    }
    CloseHandle(file);
}

void EventBridge::HandleDatagram(const char* data, int length) {
    // Fail-tolerant parse: garbage never throws out of the worker.
    try {
        const auto parsed =
            nlohmann::json::parse(data, data + length, nullptr, false);
        if (parsed.is_discarded() || !parsed.is_object()) {
            return;
        }
        const std::string type = parsed.value("type", "");
        const std::string id = parsed.value("id", "");
        if (id.empty() || id.size() > kMaxIdLength) {
            return;
        }
        if (type == "sf_flash") {
            if (pending_flashes_.size() >= kMaxPendingFlashes) {
                return;  // the worker drains these every tick; drop the burst
            }
            Flash flash;
            flash.id = id;
            flash.scale = parsed.value("scale", 1.0);
            if (flash.scale < 0.0) {
                flash.scale = 0.0;
            }
            if (flash.scale > 1.0) {
                flash.scale = 1.0;
            }
            pending_flashes_.push_back(std::move(flash));
        } else if (type == "sf_state") {
            if (values_.size() >= kMaxStateKeys &&
                values_.find(id) == values_.end()) {
                return;  // known keys keep updating; new keys stop at the cap
            }
            values_[id] = parsed.value("value", 0.0);
        }
        // Unknown types are ignored by design (forward compatibility).
    } catch (...) {
        // nlohmann with error_handler::ignore should not throw; belt and
        // suspenders for allocator edge cases — never kill the worker.
    }
}

}  // namespace senseforge

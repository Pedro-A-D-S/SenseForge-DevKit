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
        char buffer[4096];
        DWORD read = 0;
        std::string line;
        line.reserve(256);
        while (ReadFile(file, buffer, sizeof(buffer), &read, nullptr) != FALSE &&
               read > 0) {
            file_offset_ += read;
            for (DWORD i = 0; i < read; ++i) {
                const char c = buffer[i];
                if (c == '\n') {
                    if (!line.empty()) {
                        HandleDatagram(line.data(), static_cast<int>(line.size()));
                        line.clear();
                    }
                } else if (c != '\r') {
                    line.push_back(c);
                }
            }
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
        if (id.empty()) {
            return;
        }
        if (type == "sf_flash") {
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
            values_[id] = parsed.value("value", 0.0);
        }
        // Unknown types are ignored by design (forward compatibility).
    } catch (...) {
        // nlohmann with error_handler::ignore should not throw; belt and
        // suspenders for allocator edge cases — never kill the worker.
    }
}

}  // namespace senseforge

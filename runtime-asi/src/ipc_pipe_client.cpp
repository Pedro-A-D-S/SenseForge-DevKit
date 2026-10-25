// ipc_pipe_client.cpp — implementation (SF-3 · bidirectional since SF-7).

#include "ipc_pipe_client.hpp"

#include <thread>

namespace senseforge {

namespace {
constexpr std::int64_t kReconnectPeriodMs = 2000;

std::int64_t NowMs() {
    LARGE_INTEGER counter{};
    LARGE_INTEGER frequency{};
    QueryPerformanceCounter(&counter);
    QueryPerformanceFrequency(&frequency);
    return frequency.QuadPart != 0
               ? (counter.QuadPart * 1000) / frequency.QuadPart
               : 0;
}
}  // namespace

IpcPipeClient::IpcPipeClient(std::wstring pipe_name)
    : pipe_name_(std::move(pipe_name)) {}

IpcPipeClient::~IpcPipeClient() {
    const HANDLE handle = handle_.exchange(INVALID_HANDLE_VALUE);
    if (handle != INVALID_HANDLE_VALUE) {
        // The reader thread notices (handle_ changed / PeekNamedPipe
        // fails) and retires on its own.
        CloseHandle(handle);
    }
}

void IpcPipeClient::SetMessageHandler(MessageHandler handler) {
    handler_ = std::move(handler);
}

bool IpcPipeClient::Connect() {
    const std::int64_t now = NowMs();
    if (now - last_attempt_ms_ < kReconnectPeriodMs) {
        return false;  // lazy backoff — never hammer the pipe
    }
    last_attempt_ms_ = now;

    // Duplex open (SF-7): Studio pushes requests (trigger_test_effect) on
    // the same connection the runtime streams telemetry through.
    HANDLE pipe = CreateFileW(pipe_name_.c_str(),
                              GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (pipe == INVALID_HANDLE_VALUE) {
        return false;
    }
    handle_.store(pipe);
    StartReader(pipe);
    return true;
}

// Reader thread: drains Studio->runtime requests. Uses PeekNamedPipe
// polling instead of a blocking ReadFile — CloseHandle from another
// thread must never leave this thread stuck on a dead handle.
void IpcPipeClient::StartReader(HANDLE own_handle) {
    std::thread([this, own_handle] {
        std::string pending;
        char buffer[512];
        while (handle_.load() == own_handle) {
            DWORD available = 0;
            if (!PeekNamedPipe(own_handle, nullptr, 0, nullptr, &available,
                               nullptr)) {
                break;  // pipe gone (Studio closed) — reader retires
            }
            if (available == 0) {
                Sleep(5);
                continue;
            }
            DWORD to_read = available < sizeof(buffer)
                                ? available
                                : static_cast<DWORD>(sizeof(buffer));
            DWORD read = 0;
            if (!ReadFile(own_handle, buffer, to_read, &read, nullptr) ||
                read == 0) {
                break;
            }
            pending.append(buffer, read);
            // Dispatch every complete line.
            std::size_t newline = pending.find('\n');
            while (newline != std::string::npos) {
                std::string line = pending.substr(0, newline);
                pending.erase(0, newline + 1);
                if (handler_) {
                    handler_(line);
                }
                newline = pending.find('\n');
            }
        }
    }).detach();
}

bool IpcPipeClient::WriteLine(const std::string& line) {
    if (handle_.load() == INVALID_HANDLE_VALUE && !Connect()) {
        return false;  // Studio not up yet — drop the line silently
    }

    const std::string framed = line + "\n";
    DWORD written = 0;
    const HANDLE handle = handle_.load();
    if (!WriteFile(handle, framed.data(),
                   static_cast<DWORD>(framed.size()), &written, nullptr) ||
        written != framed.size()) {
        // Broken pipe (Studio closed): drop and retry on the next line.
        const HANDLE dead = handle_.exchange(INVALID_HANDLE_VALUE);
        if (dead != INVALID_HANDLE_VALUE) {
            CloseHandle(dead);
        }
        return false;
    }
    FlushFileBuffers(handle);  // push the line out of the pipe buffer
    return true;
}

}  // namespace senseforge

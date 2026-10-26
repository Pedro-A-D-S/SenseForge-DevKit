// ipc_pipe_client.hpp — telemetry client to \\.\pipe\senseforge_ipc (SF-3).
//
// The runtime (game process) is the CLIENT; SenseForge Studio's Rust backend
// (SF-7) owns the pipe server. Best-effort by design: telemetry must never
// block or crash the game — writes fail silently and the client reconnects
// lazily when the Studio comes up.

#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <windows.h>

namespace senseforge {

// SF-22: true when the process serving `pipe` (a client-side pipe handle)
// runs as the same user as the current process. The pipe name is
// machine-global and Studio is not installed on player PCs, so any local
// process can create it first; telemetry and the trigger-test request must
// not go to (or come from) another user's process. Fail-closed: any error
// while checking (including no access to that process) returns false.
bool ServerRunsAsCurrentUser(HANDLE pipe);

class IpcPipeClient {
public:
    // Called with one full JSON line for every request Studio pushes
    // (SF-7: {"type":"trigger_test_effect"}). Invoked from the reader
    // thread — handlers must be cheap and thread-safe.
    using MessageHandler = std::function<void(const std::string&)>;

    explicit IpcPipeClient(std::wstring pipe_name =
                               L"\\\\.\\pipe\\senseforge_ipc");
    ~IpcPipeClient();

    IpcPipeClient(const IpcPipeClient&) = delete;
    IpcPipeClient& operator=(const IpcPipeClient&) = delete;

    // Appends '\n' and writes one JSON line. Never blocks long: on a dead
    // pipe it schedules a reconnect at most once per 2 seconds.
    bool WriteLine(const std::string& line);

    bool IsConnected() const { return handle_ != INVALID_HANDLE_VALUE; }

    // Registers the Studio->runtime message handler (SF-7). Must be set
    // before the first WriteLine so the reader thread is always live.
    void SetMessageHandler(MessageHandler handler);

private:
    bool Connect();
    void StartReader(HANDLE handle);

    std::wstring pipe_name_;
    std::atomic<HANDLE> handle_{INVALID_HANDLE_VALUE};
    std::int64_t last_attempt_ms_ = 0;
    MessageHandler handler_;
};

}  // namespace senseforge

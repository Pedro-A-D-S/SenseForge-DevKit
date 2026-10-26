// ipc_pipe_client.cpp — implementation (SF-3 · bidirectional since SF-7).

#include "ipc_pipe_client.hpp"

#include <thread>
#include <vector>

namespace senseforge {

namespace {
constexpr std::int64_t kReconnectPeriodMs = 2000;
// A telemetry line is < 1 KB; if the server has not made room for it in this
// long, it is not reading and the connection is dropped (SF-22).
constexpr ULONGLONG kWriteBudgetMs = 20;

// Writes the whole buffer on a PIPE_NOWAIT handle without ever blocking: a
// full pipe buffer shows up as a short (or zero-byte) write, retried until
// the budget runs out.
bool WriteAllBounded(HANDLE handle, const char* data, std::size_t size) {
    const ULONGLONG deadline = GetTickCount64() + kWriteBudgetMs;
    std::size_t offset = 0;
    while (offset < size) {
        DWORD written = 0;
        if (!WriteFile(handle, data + offset,
                       static_cast<DWORD>(size - offset), &written, nullptr)) {
            return false;
        }
        offset += written;
        if (offset < size) {
            if (GetTickCount64() >= deadline) {
                return false;
            }
            Sleep(1);
        }
    }
    return true;
}

// Owner SID of a process token, as raw TOKEN_USER bytes.
bool QueryUserSid(HANDLE process, std::vector<char>* out) {
    HANDLE token = nullptr;
    if (!OpenProcessToken(process, TOKEN_QUERY, &token)) {
        return false;
    }
    DWORD needed = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &needed);
    bool ok = false;
    if (needed > 0) {
        out->assign(needed, 0);
        ok = GetTokenInformation(token, TokenUser, out->data(), needed,
                                 &needed) != FALSE;
    }
    CloseHandle(token);
    return ok;
}

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

bool ServerRunsAsCurrentUser(HANDLE pipe) {
    if (pipe == INVALID_HANDLE_VALUE || pipe == nullptr) {
        return false;
    }
    ULONG server_pid = 0;
    if (!GetNamedPipeServerProcessId(pipe, &server_pid) || server_pid == 0) {
        return false;  // not a pipe client handle
    }
    const HANDLE server_process =
        OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, server_pid);
    if (server_process == nullptr) {
        return false;  // typically another user's (or an elevated) process
    }
    std::vector<char> server_sid;
    std::vector<char> own_sid;
    const bool have_both = QueryUserSid(server_process, &server_sid) &&
                           QueryUserSid(GetCurrentProcess(), &own_sid);
    CloseHandle(server_process);
    if (!have_both) {
        return false;
    }
    return EqualSid(reinterpret_cast<TOKEN_USER*>(server_sid.data())->User.Sid,
                    reinterpret_cast<TOKEN_USER*>(own_sid.data())->User.Sid) !=
           FALSE;
}

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
    // SF-22: the pipe name is machine-global and Studio is not installed on
    // player PCs, so any local process can create it first. SQOS at the
    // identification level keeps that server from impersonating the game's
    // user token; it can still talk to us, which the reader below bounds.
    HANDLE pipe = CreateFileW(pipe_name_.c_str(),
                              GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                              OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL | SECURITY_SQOS_PRESENT |
                                  SECURITY_IDENTIFICATION,
                              nullptr);
    if (pipe == INVALID_HANDLE_VALUE) {
        return false;
    }
    // SF-22: only talk to a server that runs as this user.
    if (!ServerRunsAsCurrentUser(pipe)) {
        // Also what a Studio running elevated looks like from here.
        OutputDebugStringA(
            "SenseForge: pipe server refused (other user or elevated)\n");
        CloseHandle(pipe);
        return false;
    }
    // Non-blocking writes: a server that stops reading must not stall the
    // game's worker thread (the reader below only calls ReadFile after
    // PeekNamedPipe reports data, so it is unaffected).
    DWORD mode = PIPE_NOWAIT;
    if (!SetNamedPipeHandleState(pipe, &mode, nullptr, nullptr)) {
        // Still blocking: fail closed rather than reintroduce the stall.
        CloseHandle(pipe);
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
      // SF-22: this thread is detached, so an exception would reach
      // std::terminate and take the game down. A peer that streams bytes
      // without a newline must not grow `pending` without bound either.
      try {
        constexpr std::size_t kMaxPendingBytes = 64 * 1024;
        bool discard_next = false;  // tail of a line that overflowed
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
                if (discard_next) {
                    discard_next = false;  // end of the oversized line
                } else if (handler_) {
                    handler_(line);
                }
                newline = pending.find('\n');
            }
            if (pending.size() > kMaxPendingBytes) {
                pending.clear();
                pending.shrink_to_fit();
                discard_next = true;
            }
        }
      } catch (...) {
        // Reader retires; the next WriteLine reconnects.
      }
    }).detach();
}

bool IpcPipeClient::WriteLine(const std::string& line) {
    if (handle_.load() == INVALID_HANDLE_VALUE && !Connect()) {
        return false;  // Studio not up yet — drop the line silently
    }

    const std::string framed = line + "\n";
    const HANDLE handle = handle_.load();
    if (!WriteAllBounded(handle, framed.data(), framed.size())) {
        // Broken pipe (Studio closed) or a server that stopped reading:
        // drop the connection (a half-written line would corrupt framing)
        // and retry on the next line.
        const HANDLE dead = handle_.exchange(INVALID_HANDLE_VALUE);
        if (dead != INVALID_HANDLE_VALUE) {
            CloseHandle(dead);
        }
        return false;
    }
    // No FlushFileBuffers: it blocks until the server has read everything.
    return true;
}

}  // namespace senseforge

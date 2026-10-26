// test_ipc_pipe_client.cpp — SF-22: the game-side pipe client against a real
// named-pipe server created by the test.
//
// The pipe name is machine-global and Studio is absent on player PCs, so any
// local process may be the server. Two properties are pinned here:
//   1. the server can only obtain an identification-level token of the
//      client (SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION), never an
//      impersonation-level one;
//   2. a peer that streams bytes without a newline does not grow the
//      reader's buffer without bound, and the next real request still
//      arrives (the oversized line is dropped up to its newline).

#include "test_common.hpp"

#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "ipc_pipe_client.hpp"

using namespace senseforge;

int main() {
    const std::wstring name =
        L"\\\\.\\pipe\\sf_test_ipc_" + std::to_wstring(GetCurrentProcessId());

    const HANDLE server = CreateNamedPipeW(
        name.c_str(), PIPE_ACCESS_DUPLEX,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, 1, 1 << 20, 1 << 20, 0,
        nullptr);
    assert(server != INVALID_HANDLE_VALUE);

    std::atomic<int> impersonation_level{-1};
    std::atomic<DWORD> impersonation_error{0};
    std::atomic<bool> server_done{false};

    std::thread server_thread([&] {
        if (!ConnectNamedPipe(server, nullptr) &&
            GetLastError() != ERROR_PIPE_CONNECTED) {
            server_done = true;
            return;
        }
        // The client writes "hello\n" first; impersonation needs a read.
        char buffer[64] = {};
        DWORD read = 0;
        ReadFile(server, buffer, sizeof(buffer), &read, nullptr);

        if (ImpersonateNamedPipeClient(server) != FALSE) {
            HANDLE token = nullptr;
            if (OpenThreadToken(GetCurrentThread(), TOKEN_QUERY, TRUE, &token) !=
                FALSE) {
                SECURITY_IMPERSONATION_LEVEL level = SecurityAnonymous;
                DWORD returned = 0;
                if (GetTokenInformation(token, TokenImpersonationLevel, &level,
                                        sizeof(level), &returned) != FALSE) {
                    impersonation_level = static_cast<int>(level);
                }
                CloseHandle(token);
            }
            RevertToSelf();
        } else {
            impersonation_error = GetLastError();
        }

        // 200 KB without a newline, then the newline, then a real request.
        const std::string chunk(8192, 'x');
        DWORD written = 0;
        for (int i = 0; i < 25; ++i) {
            WriteFile(server, chunk.data(), static_cast<DWORD>(chunk.size()),
                      &written, nullptr);
        }
        const std::string tail = "\n{\"type\":\"trigger_test_effect\"}\n";
        WriteFile(server, tail.data(), static_cast<DWORD>(tail.size()), &written,
                  nullptr);
        while (!server_done) {
            Sleep(5);
        }
    });

    std::mutex mutex;
    std::vector<std::string> lines;
    {
        IpcPipeClient client(name);
        client.SetMessageHandler([&](const std::string& line) {
            std::lock_guard<std::mutex> lock(mutex);
            lines.push_back(line);
        });
        assert(client.WriteLine("hello"));

        const auto deadline =
            std::chrono::steady_clock::now() + std::chrono::seconds(10);
        bool got_request = false;
        while (!got_request && std::chrono::steady_clock::now() < deadline) {
            {
                std::lock_guard<std::mutex> lock(mutex);
                got_request = !lines.empty();
            }
            Sleep(10);
        }
        assert(got_request);
        Sleep(100);  // let any (wrongly delivered) extra line show up

        // 1. Identification only: the server cannot act as the client's user.
        assert(impersonation_error == 0);
        assert(impersonation_level == static_cast<int>(SecurityIdentification));

        // 2. The 200 KB line is dropped; only the real request arrives.
        std::lock_guard<std::mutex> lock(mutex);
        assert(lines.size() == 1);
        assert(lines[0].find("trigger_test_effect") != std::string::npos);
        assert(lines[0].size() < 1024);
    }

    server_done = true;
    server_thread.join();
    CloseHandle(server);

    // ---- SF-22: a server that never reads must not stall the writer -------
    // Tiny pipe buffers, one instance, nobody reading: before the fix the
    // game's worker blocked in WriteFile/FlushFileBuffers for good.
    {
        const std::wstring stuck_name =
            L"\\\\.\\pipe\\sf_test_stuck_" + std::to_wstring(GetCurrentProcessId());
        const HANDLE stuck_server = CreateNamedPipeW(
            stuck_name.c_str(), PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, 1, 4096, 4096, 0,
            nullptr);
        assert(stuck_server != INVALID_HANDLE_VALUE);

        IpcPipeClient client(stuck_name);
        const std::string line(900, 'y');
        const auto start = std::chrono::steady_clock::now();
        int accepted = 0;
        for (int i = 0; i < 400; ++i) {
            if (client.WriteLine(line)) {
                ++accepted;
            }
        }
        const auto elapsed = std::chrono::steady_clock::now() - start;
        assert(accepted > 0);    // the first lines fit in the pipe buffer
        assert(accepted < 400);  // ...and then it gave up instead of blocking
        assert(elapsed < std::chrono::seconds(5));
        CloseHandle(stuck_server);
    }

    // ---- SF-22: server identity helper --------------------------------------
    {
        const std::wstring id_name =
            L"\\\\.\\pipe\\sf_test_id_" + std::to_wstring(GetCurrentProcessId());
        const HANDLE id_server = CreateNamedPipeW(
            id_name.c_str(), PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, 1, 4096, 4096, 0,
            nullptr);
        assert(id_server != INVALID_HANDLE_VALUE);
        const HANDLE id_client =
            CreateFileW(id_name.c_str(), GENERIC_READ | GENERIC_WRITE, 0,
                        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        assert(id_client != INVALID_HANDLE_VALUE);
        assert(ServerRunsAsCurrentUser(id_client));  // this test is the server

        // Anything that is not a pipe client handle is refused (fail-closed).
        assert(!ServerRunsAsCurrentUser(INVALID_HANDLE_VALUE));
        assert(!ServerRunsAsCurrentUser(nullptr));
        const HANDLE not_a_pipe = CreateFileW(
            L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        assert(not_a_pipe != INVALID_HANDLE_VALUE);
        assert(!ServerRunsAsCurrentUser(not_a_pipe));
        CloseHandle(not_a_pipe);
        CloseHandle(id_client);
        CloseHandle(id_server);
    }
    return 0;
}

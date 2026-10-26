// sf_debug_logger.cpp — SF-11b crash triage instrumentation.
#include "sf_debug_logger.hpp"

#include <windows.h>

#include <dbghelp.h>
#include <shlobj.h>

#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <filesystem>
#include <mutex>
#include <string>

namespace senseforge {
namespace {

std::mutex g_log_mutex;
std::filesystem::path g_log_path;
bool g_crash_filter_installed = false;

std::filesystem::path LocalSenseForgeDir(const wchar_t* leaf) {
    wchar_t base[MAX_PATH]{};
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0,
                               base))) {
        return {};
    }
    const auto dir = std::filesystem::path(base) / L"senseforge" / leaf;
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir;
}

std::string ExeBaseName() {
    wchar_t exe[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    const auto stem = std::filesystem::path(exe).stem().string();
    std::string out;
    for (const char c : stem) {
        out += static_cast<char>(
            std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_'
                ? c
                : '_');
    }
    return out.empty() ? std::string("runtime") : out;
}

// Dump writer — C++ objects allowed (no __try in this frame). Called from
// the SEH filter below, which keeps its own frame trivial (C2712).
void WriteCrashDump(EXCEPTION_POINTERS* pointers) {
    const auto dir = LocalSenseForgeDir(L"crash");
    if (dir.empty()) {
        return;
    }
    const auto path =
        dir / (ExeBaseName() + "_" +
               std::to_string(GetCurrentProcessId()) + ".dmp");
    const HANDLE file =
        CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                    FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return;
    }
    // SF-22: system copy only (this runs inside a crashing game process; a
    // dbghelp.dll planted in the game folder must not be picked up).
    const HMODULE dbghelp =
        LoadLibraryExW(L"dbghelp.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (dbghelp != nullptr) {
        using MiniDumpWriteDumpFn =
            BOOL(WINAPI*)(HANDLE, DWORD, HANDLE, MINIDUMP_TYPE,
                          const MINIDUMP_EXCEPTION_INFORMATION*,
                          const MINIDUMP_USER_STREAM_INFORMATION*,
                          const MINIDUMP_CALLBACK_INFORMATION*);
        const auto write = reinterpret_cast<MiniDumpWriteDumpFn>(
            GetProcAddress(dbghelp, "MiniDumpWriteDump"));
        if (write != nullptr) {
            MINIDUMP_EXCEPTION_INFORMATION mei{};
            mei.ThreadId = GetCurrentThreadId();
            mei.ExceptionPointers = pointers;
            mei.ClientPointers = FALSE;
            write(GetCurrentProcess(), GetCurrentProcessId(), file,
                  MiniDumpNormal, &mei, nullptr, nullptr);
        }
        FreeLibrary(dbghelp);
    }
    CloseHandle(file);
    DebugLog("CRASH code=0x%08X addr=%p (minidump written)",
             pointers->ExceptionRecord->ExceptionCode,
             pointers->ExceptionRecord->ExceptionAddress);
}

// Last-chance handler: write the dump, log, then let WER proceed.
// dbghelp is loaded lazily HERE — never at init (loader-lock safety).
LONG WINAPI CrashDumpFilter(EXCEPTION_POINTERS* pointers) {
    __try {
        WriteCrashDump(pointers);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

}  // namespace

void DebugLoggerInit() {
    std::lock_guard<std::mutex> lock(g_log_mutex);
    if (g_log_path.empty()) {
        g_log_path =
            LocalSenseForgeDir(L"logs") /
            (ExeBaseName() + "_" + std::to_string(GetCurrentProcessId()) +
             ".log");
    }
    if (!g_crash_filter_installed) {
        SetUnhandledExceptionFilter(CrashDumpFilter);
        g_crash_filter_installed = true;
    }
    // Manual append — the mutex is already held here, so calling DebugLog
    // would self-deadlock.
    SYSTEMTIME st{};
    GetLocalTime(&st);
    FILE* f = _wfopen(g_log_path.c_str(), L"a");
    if (f != nullptr) {
        fprintf(f,
                "%02u:%02u:%02u.%03u [pid=%lu] logger init (log + crash "
                "filter ready)\n",
                st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
                GetCurrentProcessId());
        fclose(f);
    }
}

void DebugLog(const char* fmt, ...) {
    std::lock_guard<std::mutex> lock(g_log_mutex);
    if (g_log_path.empty()) {
        return;  // init never ran (or LOCALAPPDATA unavailable) — no-op
    }
    FILE* f = _wfopen(g_log_path.c_str(), L"a");
    if (f == nullptr) {
        return;
    }
    SYSTEMTIME st{};
    GetLocalTime(&st);
    fprintf(f, "%02u:%02u:%02u.%03u [pid=%lu tid=%lu] ", st.wHour,
            st.wMinute, st.wSecond, st.wMilliseconds, GetCurrentProcessId(),
            GetCurrentThreadId());
    va_list ap;
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fprintf(f, "\n");
    fclose(f);
}

}  // namespace senseforge

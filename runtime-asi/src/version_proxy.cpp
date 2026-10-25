// version.dll proxy loader (SF-3) — Ultimate-ASI-Loader style.
//
// 1. Loads the REAL C:\Windows\System32\version.dll with
//    LOAD_LIBRARY_SEARCH_SYSTEM32 (the default search order would resolve
//    "version.dll" to our own proxy in the executable directory — infinite
//    recursion) and resolves its 17 exports into g_versionProcs, which the
//    MASM thunks in version_thunks.asm forward to.
// 2. Loads every *.asi next to the game executable, which is how
//    senseforge.asi enters the game process without an external injector.

#include <windows.h>

#include <filesystem>
#include <string_view>

// Resolved from System32\version.dll during DLL_PROCESS_ATTACH (index order
// must match kExportNames below and the thunk table in version_thunks.asm).
// Defaults to FailStub so an unresolved export degrades to a zero return
// instead of an access violation (fail-closed).
extern "C" FARPROC g_versionProcs[17] = {};

namespace {

constexpr std::wstring_view kExportNames[17] = {
    L"GetFileVersionInfoA",       L"GetFileVersionInfoByHandle",
    L"GetFileVersionInfoExA",     L"GetFileVersionInfoExW",
    L"GetFileVersionInfoSizeA",   L"GetFileVersionInfoSizeExA",
    L"GetFileVersionInfoSizeExW", L"GetFileVersionInfoSizeW",
    L"GetFileVersionInfoW",       L"VerFindFileA",
    L"VerFindFileW",              L"VerInstallFileA",
    L"VerInstallFileW",           L"VerLanguageNameA",
    L"VerLanguageNameW",          L"VerQueryValueA",
    L"VerQueryValueW",
};

// Returns 0 in RAX — every version API treats 0 as failure/none, so an
// unresolved export degrades gracefully instead of jumping to null.
extern "C" __declspec(noinline) LONG64 WINAPI FailStub() {
    return 0;
}

void LoadOriginalVersionDll() {
    for (auto& proc : g_versionProcs) {
        proc = reinterpret_cast<FARPROC>(&FailStub);
    }

    // Absolute path first: immune to search-order subtleties. The plain
    // name with LOAD_LIBRARY_SEARCH_SYSTEM32 is the fallback.
    HMODULE real = LoadLibraryExW(L"C:\\Windows\\System32\\version.dll",
                                  nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (real == nullptr) {
        real = LoadLibraryExW(L"version.dll", nullptr,
                               LOAD_LIBRARY_SEARCH_SYSTEM32);
    }
    if (real == nullptr) {
        return;
    }
    for (size_t i = 0; i < 17; ++i) {
        char narrow[64]{};
        WideCharToMultiByte(CP_ACP, 0, kExportNames[i].data(),
                            static_cast<int>(kExportNames[i].size()), narrow,
                            sizeof(narrow) - 1, nullptr, nullptr);
        if (FARPROC proc = GetProcAddress(real, narrow)) {
            g_versionProcs[i] = proc;
        }
    }
}

void LoadAsiModules() {
    wchar_t exe_path[MAX_PATH]{};
    if (GetModuleFileNameW(nullptr, exe_path, MAX_PATH) == 0) {
        return;
    }
    const std::filesystem::path dir =
        std::filesystem::path(exe_path).parent_path();

    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (ec) {
            break;
        }
        if (!entry.is_regular_file(ec) || ec) {
            continue;
        }
        if (_wcsicmp(entry.path().extension().c_str(), L".asi") == 0) {
            // DllMain of the ASI runs under the loader lock; senseforge.asi
            // only spawns its worker thread and returns immediately.
            OutputDebugStringW(L"[senseforge-proxy] loading ASI: ");
            OutputDebugStringW(entry.path().c_str());
            OutputDebugStringW(L"\n");
            LoadLibraryW(entry.path().c_str());
        }
    }
}

}  // namespace

BOOL APIENTRY DllMain(HMODULE, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        OutputDebugStringW(L"[senseforge-proxy] PROCESS_ATTACH begin\n");
        LoadOriginalVersionDll();
        OutputDebugStringW(L"[senseforge-proxy] original version.dll resolved\n");
        LoadAsiModules();
        OutputDebugStringW(L"[senseforge-proxy] PROCESS_ATTACH done\n");
    }
    return TRUE;
}

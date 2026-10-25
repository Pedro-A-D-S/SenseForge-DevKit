// ue4ss_mod_exports.cpp — SF-11b: UE4SS DLL-mod entry points.
//
// UE4SS loads this runtime as ue4ss/Mods/SenseForge/dlls/main.dll and calls
// these exports by name (GetProcAddress) — no UE4SS SDK linkage needed.
// The worker thread is already spawned by DllMain (same behavior as the
// ASI deployment): start()/update() are integration hooks for the future
// UE4SS event API (SF-12b registers gameplay hooks from here).

#include <windows.h>

#include <atomic>

namespace {

std::atomic<bool> g_started{false};

}  // namespace

// Called once when UE4SS loads the mod. void* == Mod* from UE4SS — the
// signature is pointer-sized, identical ABI.
extern "C" __declspec(dllexport) void start(void* /*mod*/) {
    g_started.store(true);
    OutputDebugStringW(L"[senseforge-ue4ss] start() called — mod alive\n");
}

// Called every frame by UE4SS. Reserved for the SF-12b event pump.
extern "C" __declspec(dllexport) void update(float /*delta_time*/) {
}

// Called when the mod is unloaded (UE4SS hot-reload or shutdown).
extern "C" __declspec(dllexport) void on_unload() {
    OutputDebugStringW(L"[senseforge-ue4ss] on_unload() called\n");
}

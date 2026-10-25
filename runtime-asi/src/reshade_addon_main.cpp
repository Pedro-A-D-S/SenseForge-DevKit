// reshade_addon_main.cpp — SF-11b: ReShade add-on entry points.
//
// ReShade 6.x loads this runtime as senseforge.<game>.addon64 from the game
// directory (same route and naming as the user's RenoDX add-ons) via
// LoadLibrary — which runs our existing DllMain and spawns the SenseForge
// worker thread (identical behavior to the ASI deployment).
// AddonInit/AddonUninit are the optional ReShade lifecycle entry points
// (exact signatures from ReShade 6.8 source/addon_manager.cpp).
// SF-14: they also register the "SenseForge" tab of ReShade's overlay
// (reshade_panel.cpp); gameplay events still arrive via the UE4SS Lua
// bridge (SF-12b).

#include "reshade_imgui.hpp"

#include <atomic>

#include "reshade_panel.hpp"
#include "sf_debug_logger.hpp"

#ifndef SENSEFORGE_GAME_NAME
#define SENSEFORGE_GAME_NAME "PC"
#endif

namespace {

std::atomic<bool> g_started{false};

}  // namespace

// Shown by ReShade in the Add-ons list. ASCII only: a UTF-8 em dash was
// rendered as "?" by ReShade 6.8 (print 25/10/2026).
extern "C" __declspec(dllexport) const char* NAME =
    "SenseForge (" SENSEFORGE_GAME_NAME ")";
extern "C" __declspec(dllexport) const char* DESCRIPTION =
    "SenseForge - native DualSense adaptive triggers, HD haptics, "
    "controller speaker and lightbar for PC games. Settings: SenseForge "
    "tab.";

// ReShade lifecycle entry point: bool(HMODULE addon, HMODULE reshade).
// Must call register_addon or ReShade unloads the module again (verified
// live: "No add-on was registered ... Unloading again"). With ImGui in
// this translation unit (reshade_imgui.hpp), register_addon also fetches
// ReShade's ImGui function table — it fails if ReShade lacks ImGui 19250.
extern "C" __declspec(dllexport) bool AddonInit(HMODULE addon_module,
                                                HMODULE reshade_module) {
    senseforge::DebugLoggerInit();
    senseforge::DebugLog("AddonInit: registering with ReShade (%s)...",
                         SENSEFORGE_GAME_NAME);
    if (!reshade::register_addon(addon_module, reshade_module)) {
        senseforge::DebugLog("AddonInit: register_addon FAILED");
        return false;
    }
    reshade::register_overlay("SenseForge", &senseforge::DrawReshadePanel);
    g_started.store(true);
    senseforge::DebugLog("AddonInit: registered (SenseForge tab)");
    OutputDebugStringW(L"[senseforge-addon] AddonInit: registered\n");
    return true;
}

// ReShade lifecycle exit point: void(HMODULE addon, HMODULE reshade).
extern "C" __declspec(dllexport) void AddonUninit(HMODULE addon_module,
                                                  HMODULE reshade_module) {
    senseforge::DebugLog("AddonUninit: unregistering");
    reshade::unregister_overlay("SenseForge", &senseforge::DrawReshadePanel);
    reshade::unregister_addon(addon_module, reshade_module);
    OutputDebugStringW(L"[senseforge-addon] AddonUninit: unregistered\n");
}

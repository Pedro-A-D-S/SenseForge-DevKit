// imgui_sony_overlay.hpp — in-game overlay (SF-6), skill:sony-style-imgui-overlay.
//
// DX11 path (baseline for Assassin's Creed Odyssey / AnvilNext): hooks
// IDXGISwapChain::Present (slot 8) and ResizeBuffers (slot 13) via MinHook.
// The hook target is obtained from a throwaway device+swapchain created by
// the overlay itself — all D3D11 swapchains share the same vtable, so the
// game's Present calls route through us too (classic mod-menu technique,
// Borderless Windowed compatible). DX12 is a follow-up: its command-queue
// model needs a distinct Present path, and the baseline game is DX11.
//
// Threading: the render (Present) thread owns ImGui + the settings model;
// the worker thread talks through atomics/mutex only (RequestToggle,
// QueueControllerButtons, SetCategories, ConsumeTestEffect).

#pragma once

#include <atomic>
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

#include <dxgi.h>
#include <windows.h>

#include "dualsense_input.hpp"
#include "overlay_settings.hpp"

namespace senseforge {

class SonyOverlay {
public:
    // Loads settings from `ini_path` (created on first save) and installs
    // the Present/ResizeBuffers hooks. Returns false when D3D11 is
    // unavailable or the hook fails — the game runs without an overlay.
    bool Install(const std::filesystem::path& ini_path);
    void Uninstall();

    bool installed() const { return installed_; }
    bool visible() const { return visible_.load(); }

    // Worker → render thread.
    void RequestToggle() { toggle_pending_.store(true); }
    void QueueControllerButtons(const ControllerButtons& buttons);
    void SetCategories(const std::vector<std::string>& category_ids);
    void SetManifestPresent(bool present) { manifest_present_ = present; }

    // Worker consumes the "Testar Efeito Agora" request (applies it to the
    // HidController + audio mixer on its own cadence).
    bool ConsumeTestEffect() { return test_effect_.exchange(false); }

    // Sandbox/validation hooks.
    static int present_intercepts() { return present_intercepts_.load(); }
    static bool imgui_ready() { return imgui_ready_.load(); }

private:
    bool EnsureImGui(void* swap_chain);
    void DrawFrame();
    void DrawMenu();
    void SaveIfChanged();

    // MinHook targets (vtable slots 8 and 13 of IDXGISwapChain).
    static HRESULT STDMETHODCALLTYPE HookedPresent(IDXGISwapChain*,
                                                   UINT sync_interval,
                                                   UINT flags);
    static HRESULT STDMETHODCALLTYPE HookedResize(
        IDXGISwapChain*, UINT buffer_count, UINT width, UINT height,
        DXGI_FORMAT new_format, UINT swap_chain_flags);

    // Vtable-swap path (SF-11b): lazy third-party patchers (RivaTuner/
    // MSI Afterburner, NVIDIA overlay) rewrite the Present prologue
    // right after a process's first Present — the inline MinHook detour
    // survives exactly ONE frame (measured live: delta=1 of 10). On the
    // first intercepted Present we swap the swapchain INSTANCE's vtable
    // pointer to our slot copy (detours in 8/13) and disable the inline
    // hook while the prologue is still ours. Later calls arrive through
    // the copy — immune to code patches — and we call the ORIGINAL
    // function pointer, chaining cleanly with any later third-party
    // patch (their hook still sees every frame; nobody clobbers anyone).
    static void AdoptSwapChain(IDXGISwapChain* swap_chain);
    static HRESULT STDMETHODCALLTYPE VtablePresent(IDXGISwapChain*,
                                                   UINT sync_interval,
                                                   UINT flags);
    static HRESULT STDMETHODCALLTYPE VtableResize(
        IDXGISwapChain*, UINT buffer_count, UINT width, UINT height,
        DXGI_FORMAT new_format, UINT swap_chain_flags);
    // Shared render body for both interception paths.
    static void RenderFrame(IDXGISwapChain* swap_chain);

    std::filesystem::path ini_path_;
    OverlaySettings settings_;
    std::vector<std::string> category_ids_;
    std::mutex categories_mutex_;
    bool categories_dirty_ = false;

    std::atomic<bool> installed_{false};
    std::atomic<bool> visible_{false};
    std::atomic<bool> toggle_pending_{false};
    std::atomic<bool> test_effect_{false};
    std::atomic<bool> manifest_present_{false};
    std::mutex buttons_mutex_;
    ControllerButtons queued_buttons_;

    bool imgui_initialized_ = false;      // render thread only
    bool device_objects_dirty_ = false;    // render thread only
    bool f5_was_down_ = false;             // render thread only
    OverlaySettings last_saved_snapshot_;

    static std::atomic<bool> imgui_ready_;
    static std::atomic<int> present_intercepts_;
};

}  // namespace senseforge

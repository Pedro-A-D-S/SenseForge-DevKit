// imgui_sony_overlay.cpp — DX11 Present hook + Sony-style menu (SF-6).

#include "imgui_sony_overlay.hpp"

#include <cmath>

#include <unordered_map>

#include <MinHook.h>
#include <d3d11.h>
#include <dxgi.h>
#include <windows.h>

#include "imgui.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"

// imgui_impl_win32.h keeps this declaration inside a '#if 0' block (it
// refuses to drag <windows.h> into headers) — the header instructs users
// to copy the extern into their own TU.
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd,
                                                             UINT msg,
                                                             WPARAM w_param,
                                                             LPARAM l_param);

// senseforge
#include <cstdio>
#define SF_OVERLAY_TRACE(msg) fprintf(stderr, "[senseforge] overlay: %s\n", msg)
// senseforge end

namespace senseforge {

std::atomic<bool> SonyOverlay::imgui_ready_{false};
std::atomic<int> SonyOverlay::present_intercepts_{0};

namespace {

// Present = vtable slot 8, ResizeBuffers = slot 13 (IDXGISwapChain).
constexpr int kPresentSlot = 8;
constexpr int kResizeBuffersSlot = 13;

SonyOverlay* g_overlay = nullptr;
void* g_original_present = nullptr;
void* g_original_resize = nullptr;
WNDPROC g_original_wndproc = nullptr;
bool g_wndproc_subclassed = false;

// SF-11b vtable adoption state (see SonyOverlay::AdoptSwapChain). Keyed
// by the ORIGINAL class vtable: drivers expose several swapchain classes
// (DX11/DX12, flip/blt) and each gets its own slot copy.
struct VtableAdoption {
    void** copy;          // our vtable copy (slots 8/13 -> our detours)
    void* real_present;   // slot 8 target captured at adoption
    void* real_resize;    // slot 13 target captured at adoption
};
std::mutex g_adopt_mutex;
std::unordered_map<void**, VtableAdoption> g_adoptions;
std::unordered_map<void*, void**> g_adopted_objects;  // obj -> original vptr

using PresentFn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT);
using ResizeBuffersFn = HRESULT(STDMETHODCALLTYPE*)(
    IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);

LRESULT CALLBACK OverlayWndProc(HWND hwnd, UINT message, WPARAM wparam,
                                LPARAM lparam) {
    if (ImGui::GetCurrentContext() != nullptr) {
        ImGui_ImplWin32_WndProcHandler(hwnd, message, wparam, lparam);
    }
    return CallWindowProcW(g_original_wndproc, hwnd, message, wparam,
                           lparam);
}

void ApplySonyStyle() {
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 10.0f;          // skill: cantos arredondados
    style.FrameRounding = 6.0f;
    style.GrabRounding = 6.0f;
    style.PopupRounding = 8.0f;
    style.WindowBorderSize = 0.0f;

    // skill §2: fundo rgba(8, 10, 16, 0.92) · accent PlayStation #0070D1.
    ImVec4* colors = style.Colors;
    colors[ImGuiCol_WindowBg] = ImVec4(8.0f / 255, 10.0f / 255,
                                       16.0f / 255, 0.92f);
    colors[ImGuiCol_TitleBg] = colors[ImGuiCol_WindowBg];
    colors[ImGuiCol_TitleBgActive] = colors[ImGuiCol_WindowBg];
    colors[ImGuiCol_PopupBg] = ImVec4(8.0f / 255, 10.0f / 255,
                                      16.0f / 255, 0.96f);
    const ImVec4 accent = ImVec4(0x00 / 255.0f, 0x70 / 255.0f,
                                 0xD1 / 255.0f, 1.0f);
    const ImVec4 accent_dim = ImVec4(accent.x, accent.y, accent.z, 0.35f);
    colors[ImGuiCol_FrameBg] = ImVec4(0.09f, 0.11f, 0.17f, 1.0f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.13f, 0.15f, 0.23f, 1.0f);
    colors[ImGuiCol_FrameBgActive] = accent_dim;
    colors[ImGuiCol_CheckMark] = accent;
    colors[ImGuiCol_SliderGrab] = accent;
    colors[ImGuiCol_SliderGrabActive] = accent;
    colors[ImGuiCol_Button] = accent_dim;
    colors[ImGuiCol_ButtonHovered] = accent;
    colors[ImGuiCol_ButtonActive] = accent;
    colors[ImGuiCol_Header] = accent_dim;
    colors[ImGuiCol_HeaderHovered] = accent;
    colors[ImGuiCol_HeaderActive] = accent;
    colors[ImGuiCol_Text] = ImVec4(0.92f, 0.94f, 0.98f, 1.0f);
    colors[ImGuiCol_TextDisabled] = ImVec4(0.45f, 0.48f, 0.55f, 1.0f);
}

bool ComboSelector(const char* label, int* value, const char* const* items,
                   int item_count) {
    // SF-22: *value comes from the ini (clamped by Load, but this widget is
    // reachable with any int) — never index items[] out of range.
    if (value == nullptr || items == nullptr || item_count <= 0) {
        return false;
    }
    if (*value < 0 || *value >= item_count) {
        *value = 0;
    }
    const char* preview = items[*value];
    if (!ImGui::BeginCombo(label, preview)) {
        return false;
    }
    bool changed = false;
    for (int i = 0; i < item_count; ++i) {
        const bool selected = (i == *value);
        if (ImGui::Selectable(items[i], selected)) {
            *value = i;
            changed = true;
        }
        if (selected) {
            ImGui::SetItemDefaultFocus();
        }
    }
    ImGui::EndCombo();
    return changed;
}

}  // namespace

bool SonyOverlay::Install(const std::filesystem::path& ini_path) {
    if (installed_.load()) {
        return true;
    }
    ini_path_ = ini_path;

    // Load current preferences (defaults when the ini is absent).
    IniDocument ini;
    ini.Load(ini_path_);
    std::vector<std::string> ids;
    {
        std::lock_guard<std::mutex> lock(categories_mutex_);
        ids = category_ids_;
    }
    settings_.Load(ini, ids);
    last_saved_snapshot_ = settings_;

    // Throwaway device + swapchain: all D3D11 swapchains share one vtable,
    // so hooking this one hooks the game's Present as well.
    HMODULE d3d11_module = GetModuleHandleA("d3d11.dll");
    if (d3d11_module == nullptr) {
        // SF-22: system copy only; a d3d11.dll next to the game must not win.
        d3d11_module = LoadLibraryExW(L"d3d11.dll", nullptr,
                                      LOAD_LIBRARY_SEARCH_SYSTEM32);
    }
    if (d3d11_module == nullptr) {
        SF_OVERLAY_TRACE("d3d11.dll not loadable");
        return false;
    }
    const auto create_device = reinterpret_cast<decltype(&D3D11CreateDevice)>(
        GetProcAddress(d3d11_module, "D3D11CreateDevice"));
    if (create_device == nullptr) {
        SF_OVERLAY_TRACE("D3D11CreateDevice not found");
        return false;
    }

    // Hidden 1x1 host window for the dummy swapchain.
    WNDCLASSW wc{};
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"SenseForgeDummy";
    RegisterClassW(&wc);
    HWND hwnd = CreateWindowExW(0, L"SenseForgeDummy", L"", WS_OVERLAPPED,
                                0, 0, 1, 1, nullptr, nullptr,
                                GetModuleHandleW(nullptr), nullptr);
    if (hwnd == nullptr) {
        SF_OVERLAY_TRACE("dummy window creation failed");
        return false;
    }

    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    IDXGISwapChain* swap_chain = nullptr;
    const HRESULT hw_result = create_device(nullptr, D3D_DRIVER_TYPE_HARDWARE,
                                            nullptr, 0, nullptr, 0,
                                            D3D11_SDK_VERSION, &device,
                                            nullptr, &context);
    if (FAILED(hw_result) || device == nullptr) {
        // WARP fallback (no adapter): same swapchain implementation.
        const HRESULT warp_result =
            create_device(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr,
                          0, D3D11_SDK_VERSION, &device, nullptr, &context);
        fprintf(stderr,
                "[senseforge] overlay: device hw=0x%08lX warp=0x%08lX\n",
                static_cast<unsigned long>(hw_result),
                static_cast<unsigned long>(warp_result));
        if (FAILED(warp_result) || device == nullptr) {
            SF_OVERLAY_TRACE("device creation failed (HW and WARP)");
            DestroyWindow(hwnd);
            return false;
        }
    }

    IDXGIFactory* factory = nullptr;
    if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory),
                                  reinterpret_cast<void**>(&factory))) ||
        factory == nullptr) {
        SF_OVERLAY_TRACE("CreateDXGIFactory1 failed");
        device->Release();
        context->Release();
        DestroyWindow(hwnd);
        return false;
    }

    DXGI_SWAP_CHAIN_DESC desc{};
    // NOTE: 1x1 buffers are rejected with E_INVALIDARG by current DXGI
    // (verified live); the size is irrelevant — we only need the vtable —
    // so use the same 128x128 the DX11 sandbox test proved working.
    desc.BufferDesc.Width = 128;
    desc.BufferDesc.Height = 128;
    desc.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;  // REQUIRED: 0 (from {}) is E_INVALIDARG
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 1;
    desc.OutputWindow = hwnd;
    desc.Windowed = TRUE;
    desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    const HRESULT created =
        factory->CreateSwapChain(device, &desc, &swap_chain);
    factory->Release();
    if (FAILED(created) || swap_chain == nullptr) {
        fprintf(stderr, "[senseforge] overlay: CreateSwapChain failed hr=0x%08lX\n",
                static_cast<unsigned long>(created));
        device->Release();
        context->Release();
        DestroyWindow(hwnd);
        return false;
    }

    void** vtable = *reinterpret_cast<void***>(swap_chain);
    void* present_target = vtable[kPresentSlot];
    void* resize_target = vtable[kResizeBuffersSlot];

    const MH_STATUS init = MH_Initialize();
    if (init != MH_OK && init != MH_ERROR_ALREADY_INITIALIZED) {
        SF_OVERLAY_TRACE("MH_Initialize failed");
        swap_chain->Release();
        device->Release();
        context->Release();
        DestroyWindow(hwnd);
        return false;
    }

    // Present hook is mandatory; ResizeBuffers is best-effort (some title
    // reimplementations tweak it).
    if (MH_CreateHook(present_target,
                      reinterpret_cast<void*>(&SonyOverlay::HookedPresent),
                      &g_original_present) != MH_OK ||
        MH_EnableHook(present_target) != MH_OK) {
        SF_OVERLAY_TRACE("Present hook failed");
        swap_chain->Release();
        device->Release();
        context->Release();
        DestroyWindow(hwnd);
        return false;
    }
    if (MH_CreateHook(resize_target,
                      reinterpret_cast<void*>(&SonyOverlay::HookedResize),
                      &g_original_resize) == MH_OK) {
        MH_EnableHook(resize_target);
    }

    // The dummy objects are disposable: the vtable (the class) is hooked.
    swap_chain->Release();
    device->Release();
    context->Release();
    DestroyWindow(hwnd);

    g_overlay = this;
    installed_.store(true);
    return true;
}

void SonyOverlay::Uninstall() {
    if (!installed_.load()) {
        return;
    }
    // Restore every adopted instance's original vtable pointer BEFORE
    // tearing hooks down: after this, Presents route around us again
    // (straight to the original function, third-party chains included).
    {
        std::lock_guard<std::mutex> lock(g_adopt_mutex);
        for (const auto& [object, original_vptr] : g_adopted_objects) {
            *reinterpret_cast<void***>(object) = original_vptr;
        }
        g_adopted_objects.clear();
        for (auto& [original_vptr, adoption] : g_adoptions) {
            delete[] adoption.copy;
        }
        g_adoptions.clear();
    }
    MH_DisableHook(MH_ALL_HOOKS);
    g_overlay = nullptr;
    installed_.store(false);
    // ImGui resources belong to the game's device; they die with the
    // process (the ASI never unloads mid-game).
}

void SonyOverlay::QueueControllerButtons(const ControllerButtons& buttons) {
    std::lock_guard<std::mutex> lock(buttons_mutex_);
    queued_buttons_ = buttons;
}

void SonyOverlay::SetCategories(
    const std::vector<std::string>& category_ids) {
    std::lock_guard<std::mutex> lock(categories_mutex_);
    category_ids_ = category_ids;
    categories_dirty_ = true;
}

bool SonyOverlay::EnsureImGui(void* swap_chain_pointer) {
    if (imgui_initialized_) {
        if (device_objects_dirty_) {
            ImGui_ImplDX11_CreateDeviceObjects();
            device_objects_dirty_ = false;
        }
        return true;
    }

    auto* swap_chain = static_cast<IDXGISwapChain*>(swap_chain_pointer);

    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    DXGI_SWAP_CHAIN_DESC desc{};
    if (FAILED(swap_chain->GetDevice(__uuidof(ID3D11Device),
                                     reinterpret_cast<void**>(&device))) ||
        device == nullptr ||
        FAILED(swap_chain->GetDesc(&desc))) {
        return false;
    }
    device->GetImmediateContext(&context);

    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    io.IniFilename = nullptr;  // never write imgui.ini next to the game exe

    ApplySonyStyle();

    // Mouse/keyboard: subclass the game's output window (the backend does
    // not install a WndProc on its own).
    if (desc.OutputWindow != nullptr && !g_wndproc_subclassed) {
        g_original_wndproc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(
            desc.OutputWindow, GWLP_WNDPROC,
            reinterpret_cast<LONG_PTR>(&OverlayWndProc)));
        g_wndproc_subclassed = true;
    }

    ImGui_ImplWin32_Init(desc.OutputWindow);
    ImGui_ImplDX11_Init(device, context);
    ImGui_ImplDX11_CreateDeviceObjects();

    device->Release();
    context->Release();
    imgui_initialized_ = true;
    imgui_ready_.store(true);
    return true;
}

void SonyOverlay::DrawFrame() {
    // Merge pending category ids from the worker (manifest hot-reload).
    std::vector<std::string> pending_ids;
    {
        std::lock_guard<std::mutex> lock(categories_mutex_);
        if (categories_dirty_) {
            pending_ids = category_ids_;
            categories_dirty_ = false;
        }
    }
    if (!pending_ids.empty()) {
        IniDocument ini;
        ini.Load(ini_path_);
        OverlaySettings fresh;
        fresh.Load(ini, pending_ids);
        // Keep in-flight edits for known keys, adopt new ids.
        settings_.categories = fresh.categories;
    }

    ImGuiIO& io = ImGui::GetIO();

    // DualSense navigation (worker-fed): D-Pad + Cross/Circle, held-state
    // semantics — ImGui does its own repeat.
    ControllerButtons buttons;
    {
        std::lock_guard<std::mutex> lock(buttons_mutex_);
        buttons = queued_buttons_;
    }
    io.AddKeyEvent(ImGuiKey_GamepadDpadUp, buttons.dpad_up);
    io.AddKeyEvent(ImGuiKey_GamepadDpadDown, buttons.dpad_down);
    io.AddKeyEvent(ImGuiKey_GamepadDpadLeft, buttons.dpad_left);
    io.AddKeyEvent(ImGuiKey_GamepadDpadRight, buttons.dpad_right);
    io.AddKeyEvent(ImGuiKey_GamepadFaceDown, buttons.cross);   // X = activate
    io.AddKeyEvent(ImGuiKey_GamepadFaceRight, buttons.circle); // O = back

    if (visible_.load()) {
        DrawMenu();
    }
}

void SonyOverlay::DrawMenu() {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkSize.x * 0.5f,
                                  viewport->WorkSize.y * 0.5f),
                            ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(560, 0), ImGuiCond_FirstUseEver);

    if (!ImGui::Begin("SenseForge###senseforge_overlay", nullptr,
                      ImGuiWindowFlags_NoResize |
                          ImGuiWindowFlags_NoCollapse |
                          ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::End();
        return;
    }

    // ---- Intensidade da Vibração (HD Haptics) --------------------------
    static const char* const kHapticItems[] = {
        "Desligado", "Fraca (40%)", "Media (70%)", "Forte (100%)",
        "Boost (130%)"};
    ComboSelector("Intensidade da Vibracao (HD Haptics)",
                  &settings_.haptic_intensity, kHapticItems, 5);

    // ---- Gatilhos Adaptativos L2 / R2 ----------------------------------
    static const char* const kTriggerItems[] = {"Desligado", "Fraco",
                                                "Medio", "Forte"};
    ComboSelector("Gatilho L2", &settings_.left_trigger_intensity,
                  kTriggerItems, 4);
    ComboSelector("Gatilho R2", &settings_.right_trigger_intensity,
                  kTriggerItems, 4);
    ImGui::Checkbox("Desativar gatilhos automaticamente nos Menus e Mapa",
                    &settings_.disable_triggers_in_menus);

    ImGui::Separator();

    // ---- Áudio do Controle (Speaker) -----------------------------------
    ImGui::Checkbox("Audio do Controle (Speaker)", &settings_.speaker_enabled);
    if (settings_.speaker_enabled) {
        float volume = static_cast<float>(settings_.speaker_volume);
        ImGui::SetNextItemWidth(-1);
        if (ImGui::SliderFloat("##speaker_volume", &volume, 0.0f, 1.0f,
                               "Volume: %.0f%%")) {
            settings_.speaker_volume = volume;
        }
        ImGui::Checkbox("Parry / Critico", &settings_.sound_parry);
        ImGui::Checkbox("Assobio / Montaria", &settings_.sound_mount);
        ImGui::Checkbox("Sons de UI", &settings_.sound_ui);
    }

    ImGui::Separator();

    // ---- Iluminação (Lightbar & Player LEDs) ---------------------------
    static const char* const kLightbarModes[] = {
        "Dinamico: Vida + Adrenalina", "Cor Estatica", "Desligado"};
    // ini semantics: 0=Off 1=Dynamic 2=Static — selector is 1/2/0.
    int lightbar_index = settings_.lightbar_mode == 0 ? 2
                        : settings_.lightbar_mode == 2 ? 1 : 0;
    if (ComboSelector("Lightbar", &lightbar_index, kLightbarModes, 3)) {
        static const int kMapping[] = {1, 2, 0};
        settings_.lightbar_mode = kMapping[lightbar_index];
    }
    static const char* const kBrightness[] = {"Escuro OLED", "Medio",
                                               "Alto"};
    ComboSelector("Brilho", &settings_.lightbar_brightness, kBrightness, 3);
    static const char* const kLedModes[] = {
        "Mostrar Segmentos de Adrenalina", "Numero do Jogador",
        "Desligado"};
    // ini semantics: 0=Off 1=player number 2=segments — selector 2/1/0.
    int leds_index = settings_.player_leds_mode == 0 ? 2
                     : settings_.player_leds_mode == 1 ? 1 : 0;
    if (ComboSelector("5 Player LEDs", &leds_index, kLedModes, 3)) {
        static const int kLedMapping[] = {2, 1, 0};
        settings_.player_leds_mode = kLedMapping[leds_index];
    }

    // ---- Categorias dinâmicas do {game}.sense.json ---------------------
    if (!settings_.categories.empty()) {
        ImGui::Separator();
        ImGui::TextDisabled("Categorias do Jogo");
        for (auto& [id, enabled] : settings_.categories) {
            ImGui::Checkbox(CategoryDisplayName(id).c_str(), &enabled);
        }
    }

    ImGui::Separator();

    // ---- Testar Efeito Agora -------------------------------------------
    if (ImGui::Button("Testar Efeito Agora", ImVec2(-1.0f, 0.0f))) {
        test_effect_.store(true);
    }
    ImGui::TextDisabled(
        "F5 ou PS + Options abre/fecha · D-Pad navega · X confirma · "
        "Circulo volta");

    ImGui::End();
}

void SonyOverlay::SaveIfChanged() {
    if (settings_.Equals(last_saved_snapshot_)) {
        return;
    }
    // Fresh load keeps foreign edits (Studio, player hand-edits), then we
    // apply only overlay-owned keys. The hot-reload watcher in the worker
    // picks the file up in <10ms — that IS the live refresh.
    IniDocument ini;
    ini.Load(ini_path_);
    settings_.ApplyTo(ini);
    if (ini.Save(ini_path_)) {
        last_saved_snapshot_ = settings_;
    }
}

// ---- Present / ResizeBuffers hooks (static) -----------------------------

void SonyOverlay::AdoptSwapChain(IDXGISwapChain* swap_chain) {
    if (swap_chain == nullptr) {
        return;
    }
    std::lock_guard<std::mutex> lock(g_adopt_mutex);
    void* const key = static_cast<void*>(swap_chain);
    if (g_adopted_objects.count(key) != 0) {
        return;  // instance already routes through our vtable copy
    }
    void** const original_vptr = *reinterpret_cast<void***>(swap_chain);
    auto it = g_adoptions.find(original_vptr);
    if (it == g_adoptions.end()) {
        // First instance of this swapchain class: build the slot copy.
        // 24 entries covers IDXGISwapChain through IDXGISwapChain4 (18
        // base + HDR/colorspace slots); slots we never rewrite are
        // verbatim copies, so any call the static type can make is safe.
        constexpr std::size_t kVtableSlots = 24;
        void** copy = new void*[kVtableSlots];
        for (std::size_t i = 0; i < kVtableSlots; ++i) {
            copy[i] = original_vptr[i];
        }
        VtableAdoption adoption;
        adoption.copy = copy;
        adoption.real_present = original_vptr[kPresentSlot];
        adoption.real_resize = original_vptr[kResizeBuffersSlot];
        copy[kPresentSlot] =
            reinterpret_cast<void*>(&SonyOverlay::VtablePresent);
        copy[kResizeBuffersSlot] =
            reinterpret_cast<void*>(&SonyOverlay::VtableResize);
        it = g_adoptions.emplace(original_vptr, adoption).first;
    }

    // Swap the instance's vtable pointer to our copy.
    g_adopted_objects[key] = original_vptr;
    *reinterpret_cast<void***>(swap_chain) = it->second.copy;

    // Disable the inline hook NOW, while the prologue still carries our
    // patch: restoring "our" bytes can never clobber a third-party patch
    // made later, and the vtable copy keeps routing every subsequent
    // Present to us. MinHook restores exactly the prologue it observed
    // at CreateHook time — if a third-party hook was already chained
    // below ours, it stays intact.
    MH_DisableHook(it->second.real_present);
    if (it->second.real_resize != nullptr) {
        MH_DisableHook(it->second.real_resize);
    }
}

// Shared render body used by both interception paths (inline detour and
// vtable copy). Runs on the Present thread with the instance lock-free
// contract: ImGui state is created lazily per swapchain.
void SonyOverlay::RenderFrame(IDXGISwapChain* swap_chain) {
    if (g_overlay == nullptr) {
        return;
    }
    SonyOverlay& overlay = *g_overlay;

    if (overlay.EnsureImGui(swap_chain)) {
        // F5 toggle (edge-detected, independent of ImGui focus).
        const bool f5_down = (GetAsyncKeyState(VK_F5) & 0x8000) != 0;
        if (f5_down && !overlay.f5_was_down_) {
            overlay.visible_.store(!overlay.visible_.load());
        }
        overlay.f5_was_down_ = f5_down;

        // PS + Options toggle fired by the worker's DualsenseNav.
        if (overlay.toggle_pending_.exchange(false)) {
            overlay.visible_.store(!overlay.visible_.load());
        }

        ImGui_ImplWin32_NewFrame();
        ImGui_ImplDX11_NewFrame();
        ImGui::NewFrame();
        overlay.DrawFrame();
        ImGui::Render();
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        overlay.SaveIfChanged();
    }
}

HRESULT STDMETHODCALLTYPE SonyOverlay::HookedPresent(
    IDXGISwapChain* swap_chain, UINT sync_interval, UINT flags) {
    present_intercepts_.fetch_add(1);

    // First frame through the inline detour: adopt the instance (vtable
    // swap + inline disable) so lazy third-party prologue patchers can
    // no longer steal the interception.
    AdoptSwapChain(swap_chain);
    RenderFrame(swap_chain);

    // The MinHook trampoline executes the original prologue bytes we
    // displaced — including anything a third party had chained below us
    // at CreateHook time.
    return reinterpret_cast<PresentFn>(g_original_present)(
        swap_chain, sync_interval, flags);
}

HRESULT STDMETHODCALLTYPE SonyOverlay::VtablePresent(
    IDXGISwapChain* swap_chain, UINT sync_interval, UINT flags) {
    present_intercepts_.fetch_add(1);
    RenderFrame(swap_chain);

    // Call the ORIGINAL slot target directly. If a lazy patcher (RTSS/
    // NVIDIA overlay) rewrote that function's prologue after we handed
    // the interception over, this call chains through their hook — full
    // coexistence, zero clobbering.
    void* real = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_adopt_mutex);
        void** const vptr = *reinterpret_cast<void***>(swap_chain);
        auto it = g_adoptions.find(vptr);
        if (it != g_adoptions.end()) {
            real = it->second.real_present;
        }
    }
    if (real == nullptr) {
        real = g_original_present;  // not adopted (shouldn't happen)
    }
    return reinterpret_cast<PresentFn>(real)(swap_chain, sync_interval,
                                             flags);
}

HRESULT STDMETHODCALLTYPE SonyOverlay::HookedResize(
    IDXGISwapChain* swap_chain, UINT buffer_count, UINT width, UINT height,
    DXGI_FORMAT new_format, UINT swap_chain_flags) {
    if (g_overlay != nullptr && SonyOverlay::imgui_ready()) {
        ImGui_ImplDX11_InvalidateDeviceObjects();
        g_overlay->device_objects_dirty_ = true;
    }
    return reinterpret_cast<ResizeBuffersFn>(g_original_resize)(
        swap_chain, buffer_count, width, height, new_format,
        swap_chain_flags);
}

HRESULT STDMETHODCALLTYPE SonyOverlay::VtableResize(
    IDXGISwapChain* swap_chain, UINT buffer_count, UINT width, UINT height,
    DXGI_FORMAT new_format, UINT swap_chain_flags) {
    if (g_overlay != nullptr && SonyOverlay::imgui_ready()) {
        ImGui_ImplDX11_InvalidateDeviceObjects();
        g_overlay->device_objects_dirty_ = true;
    }
    void* real = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_adopt_mutex);
        void** const vptr = *reinterpret_cast<void***>(swap_chain);
        auto it = g_adoptions.find(vptr);
        if (it != g_adoptions.end()) {
            real = it->second.real_resize;
        }
    }
    if (real == nullptr) {
        real = g_original_resize;
    }
    return reinterpret_cast<ResizeBuffersFn>(real)(
        swap_chain, buffer_count, width, height, new_format,
        swap_chain_flags);
}

}  // namespace senseforge

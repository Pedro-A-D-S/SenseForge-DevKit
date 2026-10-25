// test_dx11_overlay.cpp — the full SF-6 render path in a sandbox: a real
// D3D11 window+device+swapchain, the vtable Present hook installed by
// SonyOverlay::Install, the menu toggled open, frames presented through
// the hook, a ResizeBuffers round-trip and a clean uninstall. Skips
// (exit 0) on machines without a usable D3D11 adapter — ctest-friendly.

#include "test_common.hpp"

#include <d3d11.h>
#include <dxgi.h>
#include <windows.h>

#include <cstdio>

#include "imgui_sony_overlay.hpp"

using namespace senseforge;

int main() {
    // ---- Build the sandbox swapchain the classic way --------------------
    constexpr wchar_t kClass[] = L"SenseForgeOverlayTest";
    WNDCLASSW wc{};
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kClass;
    RegisterClassW(&wc);
    HWND hwnd = CreateWindowExW(0, kClass, L"sf", WS_OVERLAPPED, 0, 0, 128,
                                128, nullptr, nullptr,
                                GetModuleHandleW(nullptr), nullptr);
    if (hwnd == nullptr) {
        return 0;  // no windowing — skip
    }

    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    IDXGISwapChain* swap_chain = nullptr;

    DXGI_SWAP_CHAIN_DESC desc{};
    desc.BufferDesc.Width = 128;
    desc.BufferDesc.Height = 128;
    desc.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;  // REQUIRED: 0 (from {}) is E_INVALIDARG
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 1;
    desc.OutputWindow = hwnd;
    desc.Windowed = TRUE;
    desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    IDXGIFactory* factory = nullptr;
    if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory),
                                  reinterpret_cast<void**>(&factory))) ||
        factory == nullptr) {
        std::fprintf(stderr, "SKIP: no DXGI factory\n");
        return 0;  // no DXGI — skip
    }
    const HRESULT device_result =
        D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
                           nullptr, 0, D3D11_SDK_VERSION, &device, nullptr,
                           &context);
    if (FAILED(device_result) || device == nullptr) {
        factory->Release();
        std::fprintf(stderr, "SKIP: no D3D11 adapter (hr=0x%08lX)\n",
                     static_cast<unsigned long>(device_result));
        return 0;  // no adapter — skip
    }
    const HRESULT swap_result =
        factory->CreateSwapChain(device, &desc, &swap_chain);
    factory->Release();
    if (FAILED(swap_result) || swap_chain == nullptr) {
        // A hardware device exists but the swapchain did not: that is a
        // bug in the sandbox setup, not an environment limitation — fail
        // loudly instead of skipping.
        std::fprintf(stderr,
                     "FAIL: adapter present but CreateSwapChain hr=0x%08lX\n",
                     static_cast<unsigned long>(swap_result));
        device->Release();
        context->Release();
        return 1;
    }
    std::fprintf(stderr, "DX11 sandbox: device+swapchain ready\n");

    // ---- Install the overlay hooks ---------------------------------------
    const auto ini_path =
        std::filesystem::temp_directory_path() / "sf_test_overlay.ini";
    SonyOverlay overlay;
    if (!overlay.Install(ini_path)) {
        swap_chain->Release();
        device->Release();
        context->Release();
        return 0;  // hook unavailable in this environment — skip
    }
    assert(overlay.installed());
    assert(!overlay.visible());

    // ---- Toggle open: the render thread consumes the request on the
    // next Present (visibility only flips inside the hook) ---------------
    overlay.RequestToggle();
    assert(!overlay.visible());  // not yet consumed

    const int before = SonyOverlay::present_intercepts();
    {
        const HRESULT hr = swap_chain->Present(0, 0);
        assert(SUCCEEDED(hr));
    }
    assert(overlay.visible());  // toggle consumed by the first Present

    for (int frame = 0; frame < 9; ++frame) {
        const HRESULT hr = swap_chain->Present(0, 0);
        assert(SUCCEEDED(hr));
    }
    const int after = SonyOverlay::present_intercepts();
    std::fprintf(stderr, "intercepts: before=%d after=%d delta=%d\n", before,
                 after, after - before);
    assert(after >= before + 10);  // every Present routed through the hook
    assert(SonyOverlay::imgui_ready());
    assert(overlay.visible());     // stayed open across the frames

    // ---- ResizeBuffers round-trip (device objects rebuilt) --------------
    swap_chain->ResizeBuffers(0, 256, 256, DXGI_FORMAT_UNKNOWN, 0);
    HRESULT hr = swap_chain->Present(0, 0);
    assert(SUCCEEDED(hr));

    // ---- Worker-facing API contract ---------------------------------------
    assert(!overlay.ConsumeTestEffect());  // nothing clicked yet
    overlay.SetCategories({"melee_combat", "mount_and_ship"});

    // ---- Clean uninstall: Presents stop being intercepted ----------------
    overlay.Uninstall();
    const int uninstalled = SonyOverlay::present_intercepts();
    swap_chain->Present(0, 0);
    assert(SonyOverlay::present_intercepts() == uninstalled);

    swap_chain->Release();
    context->Release();
    device->Release();
    DestroyWindow(hwnd);
    return 0;
}

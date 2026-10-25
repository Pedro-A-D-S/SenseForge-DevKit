// xinput_hook.cpp — MinHook v1.3.4 interception of XInputSetState (SF-5).

#include "xinput_hook.hpp"

#include <MinHook.h>

namespace senseforge {

XinputHook* XinputHook::instance_ = nullptr;

namespace {

constexpr const char* kXinputModules[] = {
    "xinput1_4.dll",
    "xinput1_3.dll",
    "xinput9_1_0.dll",
};

using XInputSetStateFn = DWORD(WINAPI*)(DWORD, XINPUT_VIBRATION*);

}  // namespace

bool XinputHook::Install(RumbleSink sink, SuppressQuery suppress_query) {
    if (installed_) {
        return true;
    }

    const MH_STATUS init = MH_Initialize();
    if (init != MH_OK && init != MH_ERROR_ALREADY_INITIALIZED) {
        return false;
    }

    for (const char* module_name : kXinputModules) {
        const HMODULE module = GetModuleHandleA(module_name);
        if (module == nullptr) {
            continue;  // not loaded (yet) — try the next / retry later
        }
        auto target = reinterpret_cast<void*>(
            GetProcAddress(module, "XInputSetState"));
        if (target == nullptr) {
            continue;
        }

        instance_ = this;
        sink_ = std::move(sink);
        suppress_query_ = std::move(suppress_query);

        void* original = nullptr;
        if (MH_CreateHook(target, reinterpret_cast<void*>(&HookedSetState),
                          &original) != MH_OK) {
            instance_ = nullptr;
            return false;
        }
        if (MH_EnableHook(target) != MH_OK) {
            MH_DisableHook(target);
            instance_ = nullptr;
            return false;
        }

        original_ = original;
        installed_ = true;
        return true;
    }
    return false;  // no XInput module loaded yet
}

void XinputHook::Uninstall() {
    if (!installed_) {
        return;
    }
    MH_DisableHook(MH_ALL_HOOKS);
    installed_ = false;
    original_ = nullptr;
    instance_ = nullptr;
}

// static
DWORD WINAPI XinputHook::HookedSetState(DWORD user_index,
                                          XINPUT_VIBRATION* vibration) {
    XinputHook* self = instance_;
    if (self == nullptr || self->original_ == nullptr) {
        return ERROR_DEVICE_NOT_CONNECTED;  // hook torn down mid-flight
    }

    if (vibration != nullptr) {
        const unsigned left = vibration->wLeftMotorSpeed;
        const unsigned right = vibration->wRightMotorSpeed;

        self->capture_.last_left.store(left);
        self->capture_.last_right.store(right);
        self->capture_.count.fetch_add(1);
        if (self->sink_) {
            self->sink_(static_cast<unsigned>(user_index), left, right);
        }

        // Suppress: hand the original a zeroed local — never mutate the
        // caller's struct (the game may reuse it).
        if (self->suppress_query_ && self->suppress_query_()) {
            XINPUT_VIBRATION zeroed{};
            return reinterpret_cast<XInputSetStateFn>(self->original_)(
                user_index, &zeroed);
        }
    }

    return reinterpret_cast<XInputSetStateFn>(self->original_)(user_index,
                                                               vibration);
}

}  // namespace senseforge

// xinput_hook.hpp — XInputSetState interception via MinHook (SF-5).
//
// skill:cpp-asi-dualsense-runtime §4: the game's coarse rumble is captured
// (it is immediate Shadow-Play telemetry for damage/impacts), forwarded to
// the IPC sink, and zeroed on the way to the controller when
// [Haptics] suppress_xinput_rumble=true — the HD Haptics coils own
// vibration. The caller's XINPUT_VIBRATION struct is never modified.

#pragma once

#include <atomic>
#include <functional>

// NOTE: <xinput.h> must come AFTER <windows.h> — its own include chain
// reaches winnt.h without the target-architecture macros and fails with
// "No Target Architecture" on recent Windows SDKs (verified: SDK 10.0.26100).
#include <windows.h>
#include <xinput.h>

namespace senseforge {

class XinputHook {
public:
    using RumbleSink =
        std::function<void(unsigned user_index, unsigned left, unsigned right)>;
    using SuppressQuery = std::function<bool()>;

    struct Capture {
        std::atomic<unsigned> last_left{0};
        std::atomic<unsigned> last_right{0};
        std::atomic<long long> count{0};
    };

    // Hooks XInputSetState on the first available module among
    // xinput1_4.dll, xinput1_3.dll, xinput9_1_0.dll. Returns false when
    // none is loaded yet (caller retries — games load XInput lazily).
    bool Install(RumbleSink sink, SuppressQuery suppress_query);
    void Uninstall();

    bool installed() const { return installed_; }
    const Capture& capture() const { return capture_; }

    // The installed hook (public for the sandbox test to call directly).
    static DWORD WINAPI HookedSetState(DWORD user_index,
                                       XINPUT_VIBRATION* vibration);

private:
    static XinputHook* instance_;  // single hook per process
    RumbleSink sink_;
    SuppressQuery suppress_query_;
    Capture capture_;
    bool installed_ = false;
    void* original_ = nullptr;  // trampoline to the real XInputSetState
};

}  // namespace senseforge

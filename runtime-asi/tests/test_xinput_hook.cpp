// test_xinput_hook.cpp — MinHook interception validated in a sandbox against
// the REAL System32\xinput1_4.dll (SF-5). Without a controller attached the
// call returns ERROR_DEVICE_NOT_CONNECTED, but the hook still runs: capture,
// telemetry sink and suppression all happen before the original executes.

#include "test_common.hpp"

#include <atomic>
#include <windows.h>

#include "xinput_hook.hpp"

using senseforge::XinputHook;

int main() {
    HMODULE xinput = LoadLibraryA("xinput1_4.dll");
    assert(xinput != nullptr);

    auto set_state = reinterpret_cast<DWORD(WINAPI*)(DWORD, XINPUT_VIBRATION*)>(
        GetProcAddress(xinput, "XInputSetState"));
    assert(set_state != nullptr);

    std::atomic<int> sink_calls{0};
    std::atomic<unsigned> sink_left{0};
    std::atomic<unsigned> sink_right{0};
    std::atomic<bool> suppress{true};

    XinputHook hook;
    const bool installed = hook.Install(
        [&](unsigned /*user*/, unsigned left, unsigned right) {
            sink_calls.fetch_add(1);
            sink_left.store(left);
            sink_right.store(right);
        },
        [&]() { return suppress.load(); });
    assert(installed);
    assert(hook.installed());

    // Call through the (patched) API: the hook must intercept it.
    XINPUT_VIBRATION vibration{123, 456};
    const DWORD result = set_state(0, &vibration);  // ERROR_DEVICE_NOT_CONNECTED
    assert(result == ERROR_DEVICE_NOT_CONNECTED || result == ERROR_SUCCESS);

    assert(hook.capture().count.load() == 1);
    assert(hook.capture().last_left.load() == 123);
    assert(hook.capture().last_right.load() == 456);
    assert(sink_calls.load() == 1);
    assert(sink_left.load() == 123);
    assert(sink_right.load() == 456);

    // The caller's struct is never modified (suppression copies to a local).
    assert(vibration.wLeftMotorSpeed == 123);
    assert(vibration.wRightMotorSpeed == 456);

    // Pass-through mode still captures.
    suppress.store(false);
    set_state(0, &vibration);
    assert(hook.capture().count.load() == 2);
    assert(vibration.wLeftMotorSpeed == 123);  // still untouched

    // Uninstall: the API reverts to the original, no more captures.
    hook.Uninstall();
    assert(!hook.installed());
    set_state(0, &vibration);
    assert(hook.capture().count.load() == 2);

    return 0;
}

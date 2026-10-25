// hid_controller.hpp — adaptive trigger/lightbar/LED orchestrator (SF-4).
//
// Two transports, per SENSEFORGE_SPEC.md §2.1 and skill §2:
//   1. DirectHid — the vendored dualsensitive core (DS5W): enumDevices →
//      initDeviceContext → setDeviceOutputState with trigger profiles,
//      lightbar and player LEDs.
//   2. UdpDsx    — DSX/DualSenseY-v2 JSON on 127.0.0.1:6969 (fallback when
//      the user already runs DSX, or no DualSense is directly reachable).
//
// senseforge_user.ini multipliers (spec §3.2) are applied to force/snap/
// amplitude BEFORE encoding: preset 0=Off, 1=0.5x, 2=0.75x, 3=1.0x.

#pragma once

#include <cstdint>
#include <memory>

#include "hid_report_builder.hpp"
#include "dualsense_input.hpp"
#include "ini_config.hpp"
#include "sense_json.hpp"
#include "state_engine.hpp"

namespace senseforge {

class UdpDsxClient;

class HidController {
public:
    struct Settings {
        bool enabled = true;
        std::uint16_t udp_port = 6969;
    };

    enum class Transport { None, DirectHid, UdpDsx };

    explicit HidController(Settings settings = {});
    ~HidController();

    HidController(const HidController&) = delete;
    HidController& operator=(const HidController&) = delete;

    // Probes the DualSense through the dualsensitive core. Re-probes lazily
    // (at most once per 2s) so the worker loop can call it every tick.
    void EnsureConnected();

    Transport active_transport() const { return transport_; }
    bool device_connected() const { return device_ != nullptr; }

    // Intensity presets from senseforge_user.ini (spec §3.2):
    // 0=Off (0.0x) · 1=Weak (0.5x) · 2=Medium (0.75x) · 3=Strong (1.0x).
    void SetIntensityPresets(int left_preset, int right_preset);

    // Sends the command through the active transport. Identical consecutive
    // commands are deduped (no HID spam at 60Hz). Returns false only when
    // both transports failed.
    bool Apply(const HidOutputCommand& command);

    // Resets both triggers to Normal on the active transport.
    void Reset();

    // Reads the DualSense input state through the vendored dualsensitive
    // core (DirectHid transport only). Normalizes the buttons the overlay
    // cares about (SF-6): PS, Options, Cross, Circle and D-Pad. Returns
    // false when no controller is directly connected — fail-closed, the
    // overlay simply falls back to keyboard + mouse.
    bool TryReadInput(ControllerButtons& out);

private:
    bool SendDirect(const HidOutputCommand& command);
    bool SendUdp(const HidOutputCommand& command);

    Settings settings_;
    Transport transport_ = Transport::None;
    void* device_ = nullptr;  // DS5W::DeviceContext* (opaque here)
    std::int64_t last_probe_ms_ = -10'000;
    double left_scale_ = 1.0;
    double right_scale_ = 1.0;
    std::unique_ptr<UdpDsxClient> udp_;
    bool has_last_command_ = false;
    HidOutputCommand last_command_{};
};

// ---- Manifest → command composition (unit-testable, SF-4) ----------------

// Maps a manifest TriggerEffect (spec §3.1) into the native encoding,
// resolving:
//   * bow_elastic_scaled with a live scale_binding → force interpolated
//     base_force→max_force by the binding value (0.0-1.0);
//   * full draw (value >= full_draw_threshold, when
//     full_draw_vibration_hz > 0) → Vibration effect;
//   * lighting_rules: health_and_adrenaline lerp low→high rgb by the
//     "player_health_ratio" binding, adrenaline-full pulse color, and the
//     player LEDs bitmask from the player_leds_binding value;
//   * [Lighting] overrides from senseforge_user.ini (0=Off, 1=Dynamic,
//     2=Static color + brightness preset).
HidOutputCommand ComposeCommand(const Manifest& manifest, const State& state,
                               const BindingValues& values,
                               const IniDocument& ini);

// SF-14 per-effect overrides written by the ReShade panel.
// [States] <id>_enabled (default true): a disabled state never wins.
bool StateEnabled(const IniDocument& ini, const std::string& state_id);
// [States] <id>_<side>_force, side "left"/"right": 1-8, or 0 when absent or
// out of range (keep the manifest force).
int StateForceOverride(const IniDocument& ini, const std::string& state_id,
                       const std::string& side);
// [EventEffects] <id>_enabled (default true).
bool EventEnabled(const IniDocument& ini, const std::string& event_id);
// Overlays a gameplay flash on `command` at fade factor k (0-1):
// lightbar (unless <id>_lightbar=false), rumble motors and the optional
// trigger pulse, the last two scaled by <id>_intensity (0-150%, default
// 100). Returns false and leaves `command` untouched when the event is
// disabled.
bool ApplyEventEffect(const EventEffect& effect, double k,
                      const IniDocument& ini, HidOutputCommand& command);

}  // namespace senseforge

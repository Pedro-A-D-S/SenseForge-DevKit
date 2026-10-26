// hid_controller.cpp — dualsensitive direct transport + DSX UDP fallback
// (SF-4). The direct path drives the vendored dualsensitive core (DS5W)
// with the same trigger profiles our hid_report_builder encodes, so both
// transports are provably equivalent at the byte level.

#include "hid_controller.hpp"
#include "safe_util.hpp"

#include <windows.h>

#include <cstring>

// dualsensitive core (vendored third_party/dualsensitive, MIT).
#include <DSW_Api.h>
#include <Device.h>
#include <DS5State.h>
#include <IO.h>

#include "udp_dsx_client.hpp"
#include "sf_debug_logger.hpp"

namespace senseforge {
namespace {

std::int64_t NowMs() {
    LARGE_INTEGER counter{};
    LARGE_INTEGER frequency{};
    QueryPerformanceCounter(&counter);
    QueryPerformanceFrequency(&frequency);
    return frequency.QuadPart != 0
               ? (counter.QuadPart * 1000) / frequency.QuadPart
               : 0;
}

double PresetToScale(int preset) {
    switch (preset) {
        case 0:
            return 0.0;   // Off
        case 1:
            return 0.5;   // Weak
        case 2:
            return 0.75;  // Medium
        case 4:
            return 1.25;  // Maximum (SF-14 / TIM-68): above the manifest
        case 3:
        default:
            return 1.0;   // Strong (Sony default)
    }
}

int ScaleForce(int force, double scale) {
    if (!(scale > 0.0) || force <= 0) {  // also rejects NaN
        return 0;  // -> encoding degrades to Off (fail-closed)
    }
    return ClampInt(force * scale, 1, 8);
}

TriggerParams ScaleParams(TriggerParams params, double scale) {
    params.force = ScaleForce(params.force, scale);
    params.snap_force = ScaleForce(params.snap_force, scale);
    params.amplitude = ScaleForce(params.amplitude, scale);
    return params;
}

TriggerEffectMode MapMode(TriggerMode mode) {
    switch (mode) {
        case TriggerMode::Resistance:
            return TriggerEffectMode::Resistance;
        case TriggerMode::RigidSection:
            return TriggerEffectMode::RigidSection;
        case TriggerMode::WeaponSnap:
            return TriggerEffectMode::WeaponSnap;
        case TriggerMode::BowElasticScaled:
            return TriggerEffectMode::BowElasticScaled;
        case TriggerMode::Vibration:
            return TriggerEffectMode::Vibration;
        case TriggerMode::Off:
        default:
            return TriggerEffectMode::Off;
    }
}

TriggerParams ParamsFromManifest(const TriggerEffect& effect) {
    TriggerParams params;
    params.start_position = effect.start_position;
    params.end_position = effect.end_position;
    params.force =
        (effect.mode == TriggerMode::BowElasticScaled && effect.base_force > 0)
            ? effect.base_force
            : effect.force;
    params.snap_force =
        (effect.mode == TriggerMode::BowElasticScaled && effect.max_force > 0)
            ? effect.max_force
            : effect.force;
    params.amplitude = effect.force;
    params.frequency_hz = effect.full_draw_vibration_hz;
    return params;
}

// dualsensitive TriggerProfile + extras for a native mode (direct path).
void FillProfile(DS5W::TriggerSetting& setting,
                 TriggerEffectMode mode, const TriggerParams& params) {
    setting.extras.clear();
    switch (mode) {
        case TriggerEffectMode::Resistance:
            setting.profile = TriggerProfile::Resistance;
            setting.extras = {static_cast<unsigned char>(params.start_position),
                              static_cast<unsigned char>(params.force)};
            break;
        case TriggerEffectMode::RigidSection:
        case TriggerEffectMode::WeaponSnap:
            // dualsensitive Weapon profile: wall between start/end.
            setting.profile = TriggerProfile::Weapon;
            setting.extras = {static_cast<unsigned char>(params.start_position),
                              static_cast<unsigned char>(params.end_position),
                              static_cast<unsigned char>(params.force)};
            break;
        case TriggerEffectMode::BowElasticScaled:
            setting.profile = TriggerProfile::Bow;
            setting.extras = {static_cast<unsigned char>(params.start_position),
                              static_cast<unsigned char>(params.end_position),
                              static_cast<unsigned char>(params.force),
                              static_cast<unsigned char>(params.snap_force)};
            break;
        case TriggerEffectMode::Vibration:
            setting.profile = TriggerProfile::Vibration;
            setting.extras = {static_cast<unsigned char>(params.start_position),
                              static_cast<unsigned char>(params.amplitude),
                              static_cast<unsigned char>(params.frequency_hz)};
            break;
        case TriggerEffectMode::Off:
        default:
            setting.profile = TriggerProfile::Normal;
            break;
    }
}

// DSX JSON trigger parameters for a native mode (fallback path).
std::vector<int> DsxParamsFor(TriggerEffectMode mode,
                              const TriggerParams& params) {
    switch (mode) {
        case TriggerEffectMode::Resistance:
            return {params.start_position, params.force};
        case TriggerEffectMode::RigidSection:
        case TriggerEffectMode::WeaponSnap:
            // DSX has no dedicated wall mode — SemiAutomaticGun(start,end,force)
            // is the documented approximation (see PR notes).
            return {params.start_position, params.end_position, params.force};
        case TriggerEffectMode::BowElasticScaled:
            return {params.start_position, params.end_position, params.force,
                    params.snap_force};
        case TriggerEffectMode::Vibration:
            // VibrateTrigger takes an intensity 0-255.
            return {std::min(255, params.amplitude * 31)};
        case TriggerEffectMode::Off:
        default:
            return {};
    }
}

DsxTriggerMode DsxModeFor(TriggerEffectMode mode) {
    switch (mode) {
        case TriggerEffectMode::Resistance:
            return DsxTriggerMode::Resistance;
        case TriggerEffectMode::RigidSection:
        case TriggerEffectMode::WeaponSnap:
            return DsxTriggerMode::SemiAutomaticGun;
        case TriggerEffectMode::BowElasticScaled:
            return DsxTriggerMode::Bow;
        case TriggerEffectMode::Vibration:
            return DsxTriggerMode::VibrateTrigger;
        case TriggerEffectMode::Off:
        default:
            return DsxTriggerMode::Normal;
    }
}

double LookupValue(const BindingValues& values, const std::string& key) {
    const auto it = values.find(key);
    return it == values.end()
               ? std::numeric_limits<double>::quiet_NaN()
               : it->second;
}

bool SameCommand(const HidOutputCommand& a, const HidOutputCommand& b) {
    return a.left_mode == b.left_mode && a.right_mode == b.right_mode &&
           std::memcmp(&a.left, &b.left, sizeof(TriggerParams)) == 0 &&
           std::memcmp(&a.right, &b.right, sizeof(TriggerParams)) == 0 &&
           std::memcmp(a.lightbar_rgb, b.lightbar_rgb, 3) == 0 &&
           a.player_leds == b.player_leds &&
           a.player_led_brightness == b.player_led_brightness &&
           a.disable_leds == b.disable_leds &&
           a.motor_left == b.motor_left &&
           a.motor_right == b.motor_right;
}

// Read-failure streak for transition logging (worker thread only).
int g_read_fail_streak = 0;

}  // namespace

HidController::HidController(Settings settings) : settings_(settings) {}

HidController::~HidController() {
    if (device_ != nullptr) {
        Reset();
        DS5W::freeDeviceContext(
            static_cast<DS5W::DeviceContext*>(device_));
        device_ = nullptr;
    }
}

void HidController::EnsureConnected() {
    if (device_ != nullptr) {
        return;
    }
    const std::int64_t now = NowMs();
    if (now - last_probe_ms_ < 2000) {
        return;  // lazy re-probe cadence
    }
    last_probe_ms_ = now;

    DS5W::DeviceEnumInfo infos[16]{};
    unsigned int count = 0;
    const DS5W_ReturnValue enum_result = DS5W::enumDevices(infos, 16, &count, true);
    if (DS5W_FAILED(enum_result) || count == 0) {
        senseforge::DebugLog("HID enum: result=%d count=%u",
                            static_cast<int>(enum_result), count);
        return;
    }

    auto* context = new DS5W::DeviceContext{};
    const DS5W_ReturnValue init_result =
        DS5W::initDeviceContext(&infos[0], context);
    if (DS5W_FAILED(init_result)) {
        senseforge::DebugLog("HID initDeviceContext: FAILED result=%d",
                             static_cast<int>(init_result));
        delete context;
        return;
    }
    device_ = context;
    transport_ = Transport::DirectHid;
    senseforge::DebugLog(
        "HID connected: conn=%d inLen=%u outLen=%u",
        static_cast<int>(context->_internal.connection),
        context->_internal.inputReportLen,
        context->_internal.outputReportLen);
}

void HidController::SetIntensityPresets(int left_preset, int right_preset) {
    left_scale_ = PresetToScale(left_preset);
    right_scale_ = PresetToScale(right_preset);
}

bool HidController::Apply(const HidOutputCommand& command) {
    HidOutputCommand scaled = command;
    scaled.left = ScaleParams(scaled.left, left_scale_);
    scaled.right = ScaleParams(scaled.right, right_scale_);
    if (left_scale_ <= 0.0) {
        scaled.left_mode = TriggerEffectMode::Off;
    }
    if (right_scale_ <= 0.0) {
        scaled.right_mode = TriggerEffectMode::Off;
    }

    if (has_last_command_ && SameCommand(scaled, last_command_)) {
        return true;  // dedup — no HID/UDP spam at 60Hz
    }
    {
        static std::int64_t last_apply_log_ms = 0;
        const std::int64_t now_ms = NowMs();
        if (now_ms - last_apply_log_ms >= 1000) {
            last_apply_log_ms = now_ms;
            senseforge::DebugLog(
                "HID apply: L=%d(f%d) R=%d(f%d) rgb=%u,%u,%u leds=0x%02x",
                static_cast<int>(scaled.left_mode), scaled.left.force,
                static_cast<int>(scaled.right_mode), scaled.right.force,
                scaled.lightbar_rgb[0], scaled.lightbar_rgb[1],
                scaled.lightbar_rgb[2], scaled.player_leds);
        }
    }

    bool delivered = false;
    if (device_ != nullptr) {
        delivered = SendDirect(scaled);
    }
    if (!delivered) {
        delivered = SendUdp(scaled);
    }
    if (delivered) {
        has_last_command_ = true;
        last_command_ = scaled;
    }
    return delivered;
}

bool HidController::TryReadInput(ControllerButtons& out) {
    out = ControllerButtons{};
    if (transport_ != Transport::DirectHid || device_ == nullptr) {
        return false;
    }
    auto* context = static_cast<DS5W::DeviceContext*>(device_);
    DS5W::DS5InputState state{};
    const DS5W_ReturnValue read_result =
        DS5W::getDeviceInputState(context, &state);
    if (DS5W_FAILED(read_result)) {
        ++g_read_fail_streak;
        if (g_read_fail_streak == 1 || g_read_fail_streak % 200 == 0) {
            senseforge::DebugLog("HID read FAIL streak=%d result=%d",
                                 g_read_fail_streak,
                                 static_cast<int>(read_result));
        }
        return false;
    }
    if (g_read_fail_streak != 0) {
        senseforge::DebugLog("HID read RECOVERED after %d fails",
                             g_read_fail_streak);
        g_read_fail_streak = 0;
    }
    // Bit semantics from the vendored DS5W input defines (DS5State.h):
    // PS=BTN_B_PLAYSTATION_LOGO · Options=BTN_A_MENU · Face/DPad in the
    // decoded buttonsAndDpad bitmask.
    out.ps = (state.buttonsB & DS5W_ISTATE_BTN_B_PLAYSTATION_LOGO) != 0;
    out.options = (state.buttonsA & DS5W_ISTATE_BTN_A_MENU) != 0;
    out.cross = (state.buttonsAndDpad & DS5W_ISTATE_BTX_CROSS) != 0;
    out.circle = (state.buttonsAndDpad & DS5W_ISTATE_BTX_CIRCLE) != 0;
    out.dpad_up = (state.buttonsAndDpad & DS5W_ISTATE_DPAD_UP) != 0;
    out.dpad_down = (state.buttonsAndDpad & DS5W_ISTATE_DPAD_DOWN) != 0;
    out.dpad_left = (state.buttonsAndDpad & DS5W_ISTATE_DPAD_LEFT) != 0;
    out.dpad_right = (state.buttonsAndDpad & DS5W_ISTATE_DPAD_RIGHT) != 0;
    return true;
}

bool HidController::SendDirect(const HidOutputCommand& command) {
    auto* context = static_cast<DS5W::DeviceContext*>(device_);
    if (context == nullptr) {
        return false;
    }

    DS5W::DS5OutputState state{};
    ZeroMemory(&state, sizeof(state));
    state.triggerSettingEnabled = true;
    FillProfile(state.leftTriggerSetting, command.left_mode, command.left);
    FillProfile(state.rightTriggerSetting, command.right_mode, command.right);
    state.lightbar = DS5W::Color{command.lightbar_rgb[0],
                                 command.lightbar_rgb[1],
                                 command.lightbar_rgb[2]};
    state.playerLeds.bitmask = command.player_leds;
    state.playerLeds.playerLedFade = false;
    state.playerLeds.brightness =
        static_cast<DS5W::LedBrightness>(command.player_led_brightness);
    state.disableLeds = command.disable_leds;
    state.leftRumble = command.motor_left;
    state.rightRumble = command.motor_right;

    const DS5W_ReturnValue write_result =
        DS5W::setDeviceOutputState(context, &state);
    if (DS5W_FAILED(write_result)) {
        senseforge::DebugLog(
            "HID write FAIL result=%d (dropping context, -> re-probe)",
            static_cast<int>(write_result));
        // Handle is closed inside DS5W on write failure — drop the context.
        delete context;
        device_ = nullptr;
        transport_ = Transport::None;
        return false;
    }
    transport_ = Transport::DirectHid;
    return true;
}

bool HidController::SendUdp(const HidOutputCommand& command) {
    if (!udp_) {
        udp_ = std::make_unique<UdpDsxClient>(settings_.udp_port);
    }
    bool ok = true;
    ok = udp_->Send(UdpDsxClient::BuildTriggerJson(
                        0, /*right=*/false, DsxModeFor(command.left_mode),
                        DsxParamsFor(command.left_mode, command.left))) &&
          ok;
    ok = udp_->Send(UdpDsxClient::BuildTriggerJson(
                        0, /*right=*/true, DsxModeFor(command.right_mode),
                        DsxParamsFor(command.right_mode, command.right))) &&
          ok;
    ok = udp_->Send(UdpDsxClient::BuildRgbJson(
                        0, command.lightbar_rgb[0], command.lightbar_rgb[1],
                        command.lightbar_rgb[2])) &&
          ok;
    ok = udp_->Send(UdpDsxClient::BuildPlayerLedJson(0,
                                                       command.player_leds)) &&
          ok;
    if (ok) {
        transport_ = Transport::UdpDsx;
    }
    return ok;
}

void HidController::Reset() {
    HidOutputCommand normal{};
    normal.left_mode = TriggerEffectMode::Off;
    normal.right_mode = TriggerEffectMode::Off;
    if (device_ != nullptr) {
        SendDirect(normal);
    } else if (udp_) {
        SendUdp(normal);
    }
    has_last_command_ = false;
}

HidOutputCommand ComposeCommand(const Manifest& manifest, const State& state,
                                const BindingValues& values,
                                const IniDocument& ini) {
    HidOutputCommand command;

    // ---- Adaptive triggers -----------------------------------------------
    command.left_mode = MapMode(state.left_trigger.mode);
    command.left = ParamsFromManifest(state.left_trigger);
    command.right_mode = MapMode(state.right_trigger.mode);
    command.right = ParamsFromManifest(state.right_trigger);

    // SF-14: per-state force override from the ReShade panel ([States]
    // <id>_left_force / <id>_right_force, 1-8; 0/absent = manifest value).
    const int left_override = StateForceOverride(ini, state.id, "left");
    if (left_override > 0) {
        command.left.force = left_override;
    }
    const int right_override = StateForceOverride(ini, state.id, "right");
    if (right_override > 0) {
        command.right.force = right_override;
    }

    // bow_elastic_scaled: force interpolates with the scale binding; past
    // full_draw_threshold the effect becomes the full-draw vibration.
    for (int side = 0; side < 2; ++side) {
        TriggerEffectMode& mode =
            side == 0 ? command.left_mode : command.right_mode;
        TriggerParams& params = side == 0 ? command.left : command.right;
        const TriggerEffect& effect =
            side == 0 ? state.left_trigger : state.right_trigger;

        if (mode != TriggerEffectMode::BowElasticScaled ||
            effect.scale_binding.empty()) {
            continue;
        }
        const double tension = LookupValue(values, effect.scale_binding);
        if (std::isnan(tension)) {
            continue;  // binding unhealthy — keep base force (fail-closed)
        }
        const double t = std::clamp(tension, 0.0, 1.0);
        // Out-of-range manifest forces stay out of range (the encoder turns
        // them into Off) but the double -> int conversion is always defined.
        params.force = ClampInt(effect.base_force +
                                    (effect.max_force - effect.base_force) * t,
                                0, 1000);
        if (effect.full_draw_vibration_hz > 0 && t >= effect.full_draw_threshold) {
            mode = TriggerEffectMode::Vibration;
            params.amplitude = std::max(params.force, 1);
            params.frequency_hz = effect.full_draw_vibration_hz;
        }
    }

    // ---- Lighting ([Lighting] from senseforge_user.ini) ------------------
    const int lightbar_mode =
        static_cast<int>(ini.GetInt("Lighting", "lightbar_mode", 1));  // 0 off 1 dynamic 2 static
    const int brightness_preset =
        static_cast<int>(ini.GetInt("Lighting", "lightbar_brightness", 1));  // 0 dim 1 medium 2 bright
    command.player_led_brightness =
        brightness_preset == 0 ? 2 : (brightness_preset == 2 ? 0 : 1);

    if (lightbar_mode == 2) {
        const std::string rgb_text =
            ini.GetString("Lighting", "static_color_rgb", "0,112,209");
        unsigned int r = 0, g = 0, b = 0;
        if (std::sscanf(rgb_text.c_str(), "%u,%u,%u", &r, &g, &b) == 3) {
            command.lightbar_rgb[0] = ClampByte(static_cast<double>(r));
            command.lightbar_rgb[1] = ClampByte(static_cast<double>(g));
            command.lightbar_rgb[2] = ClampByte(static_cast<double>(b));
        }
    } else if (lightbar_mode == 1 &&
               manifest.lighting_rules.lightbar_mode ==
                   "health_and_adrenaline") {
        const double health =
            LookupValue(values, "player_health_ratio");
        if (!std::isnan(health)) {
            const double h = std::clamp(health, 0.0, 1.0);
            const auto& high = manifest.lighting_rules.high_health_rgb;
            const auto& low = manifest.lighting_rules.low_health_rgb;
            for (int i = 0; i < 3; ++i) {
                command.lightbar_rgb[i] =
                    ClampByte(low[i] + (high[i] - low[i]) * h);
            }
        }
        const std::string& adrenaline_binding =
            manifest.lighting_rules.player_leds_binding;
        if (!adrenaline_binding.empty()) {
            const double adrenaline = LookupValue(values, adrenaline_binding);
            const auto& pulse =
                manifest.lighting_rules.adrenaline_full_pulse_rgb;
            if (!std::isnan(adrenaline) && adrenaline >= 5.0) {
                for (int i = 0; i < 3; ++i) {
                    command.lightbar_rgb[i] =
                        ClampByte(static_cast<double>(pulse[i]));
                }
            }
        }
    }

    // ---- Player LEDs (0 off · 1 player number · 2 binding segments) ------
    const int leds_mode =
        static_cast<int>(ini.GetInt("Lighting", "player_leds_mode", 2));
    if (leds_mode == 1) {
        command.player_leds = 0x01;
    } else if (leds_mode == 2) {
        const std::string& binding =
            manifest.lighting_rules.player_leds_binding;
        if (!binding.empty()) {
            const double segments = LookupValue(values, binding);
            if (!std::isnan(segments)) {
                const int n = ClampInt(segments, 0, 5);
                command.player_leds = static_cast<std::uint8_t>(
                    n >= 5 ? 0x1F : ((1 << n) - 1));
            }
        }
    }

    return command;
}

bool StateEnabled(const IniDocument& ini, const std::string& state_id) {
    return ini.GetBool("States", state_id + "_enabled", true);
}

int StateForceOverride(const IniDocument& ini, const std::string& state_id,
                       const std::string& side) {
    const long long force =
        ini.GetInt("States", state_id + "_" + side + "_force", 0);
    return force >= 1 && force <= 8 ? static_cast<int>(force) : 0;
}

bool EventEnabled(const IniDocument& ini, const std::string& event_id) {
    return ini.GetBool("EventEffects", event_id + "_enabled", true);
}

bool ApplyEventEffect(const EventEffect& effect, double k,
                      const IniDocument& ini, HidOutputCommand& command) {
    if (!EventEnabled(ini, effect.id)) {
        return false;
    }
    const double intensity = std::clamp(
        ini.GetFloat("EventEffects", effect.id + "_intensity", 100.0), 0.0,
        150.0);
    if (ini.GetBool("EventEffects", effect.id + "_lightbar", true)) {
        for (int i = 0; i < 3; ++i) {
            command.lightbar_rgb[i] = ClampByte(effect.lightbar_rgb[i] * k);
        }
    }
    // Rumble and trigger pulse follow the panel intensity (0-150%).
    const double feel = k * intensity / 100.0;
    command.motor_left = ClampByte(effect.motor_left * feel);
    command.motor_right = ClampByte(effect.motor_right * feel);
    if (effect.trigger_amplitude > 0 && effect.trigger_frequency_hz > 0 &&
        feel > 0.0) {
        const int amplitude = ClampInt(effect.trigger_amplitude * feel, 1, 8);
        command.left_mode = TriggerEffectMode::Vibration;
        command.right_mode = TriggerEffectMode::Vibration;
        command.left.amplitude = amplitude;
        command.right.amplitude = amplitude;
        command.left.frequency_hz = effect.trigger_frequency_hz;
        command.right.frequency_hz = effect.trigger_frequency_hz;
    }
    return true;
}

}  // namespace senseforge

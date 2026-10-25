// test_hid_controller.cpp — ComposeCommand (manifest→native mapping with
// scale bindings and lighting rules) + HidController Apply pipeline:
// intensity presets, DSX fallback packets and dedup, over a live UDP
// listener (SF-4).

#include "test_common.hpp"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>

#include "hid_controller.hpp"

using namespace senseforge;

namespace {

Manifest FixtureManifest() {
    Manifest manifest;
    manifest.lighting_rules.lightbar_mode = "health_and_adrenaline";
    manifest.lighting_rules.high_health_rgb = {20, 210, 80};
    manifest.lighting_rules.low_health_rgb = {230, 25, 25};
    manifest.lighting_rules.adrenaline_full_pulse_rgb = {255, 195, 0};
    manifest.lighting_rules.player_leds_binding = "adrenaline_segments";

    State bow;
    bow.id = "bow_aim_and_draw";
    bow.condition = "is_aiming_bow == 1";
    bow.right_trigger.mode = TriggerMode::BowElasticScaled;
    bow.right_trigger.start_position = 1;
    bow.right_trigger.end_position = 8;
    bow.right_trigger.base_force = 3;
    bow.right_trigger.max_force = 8;
    bow.right_trigger.scale_binding = "bow_draw_tension";
    bow.right_trigger.full_draw_vibration_hz = 38;
    bow.right_trigger.full_draw_threshold = 0.95;
    bow.left_trigger.mode = TriggerMode::Resistance;
    bow.left_trigger.start_position = 1;
    bow.left_trigger.force = 3;
    manifest.states.push_back(bow);
    return manifest;
}

IniDocument FixtureIni() {
    const auto path =
        std::filesystem::temp_directory_path() / "sf_test_hc.ini";
    {
        std::ofstream out(path, std::ios::trunc);
        out << "[General]\n[AdaptiveTriggers]\nleft_trigger_intensity=3\n"
               "right_trigger_intensity=3\n[Lighting]\nlightbar_mode=1\n"
               "lightbar_brightness=1\nplayer_leds_mode=2\n";
    }
    IniDocument ini;
    ini.Load(path);
    return ini;
}

std::uint16_t BindListener(SOCKET* out) {
    WSADATA wsa{};
    WSAStartup(MAKEWORD(2, 2), &wsa);
    *out = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    bind(*out, reinterpret_cast<const sockaddr*>(&address), sizeof(address));
    sockaddr_in bound{};
    int size = sizeof(bound);
    getsockname(*out, reinterpret_cast<sockaddr*>(&bound), &size);
    DWORD timeout_ms = 2000;
    setsockopt(*out, SOL_SOCKET, SO_RCVTIMEO,
               reinterpret_cast<const char*>(&timeout_ms), sizeof(timeout_ms));
    return ntohs(bound.sin_port);
}

std::string ReceiveJson(SOCKET sock) {
    char buffer[2048]{};
    const int received =
        recvfrom(sock, buffer, sizeof(buffer) - 1, 0, nullptr, nullptr);
    return received <= 0 ? std::string() : std::string(buffer, received);
}

}  // namespace

int main() {
    const Manifest manifest = FixtureManifest();
    const IniDocument ini = FixtureIni();
    const State& bow = manifest.states[0];

    // ---- ComposeCommand: bow scaling by binding tension ------------------
    {
        BindingValues values{{"bow_draw_tension", 0.5}};
        const HidOutputCommand cmd = ComposeCommand(manifest, bow, values, ini);
        assert(cmd.right_mode == TriggerEffectMode::BowElasticScaled);
        // force = round(3 + (8-3) * 0.5) = 6; snap = max_force = 8.
        assert(cmd.right.force == 6);
        assert(cmd.right.snap_force == 8);
        assert(cmd.left_mode == TriggerEffectMode::Resistance);
        assert(cmd.left.force == 3);
        assert(cmd.left.start_position == 1);
    }
    {
        // Full draw (tension >= 0.95) flips the right trigger to Vibration.
        BindingValues values{{"bow_draw_tension", 0.97}};
        const HidOutputCommand cmd = ComposeCommand(manifest, bow, values, ini);
        assert(cmd.right_mode == TriggerEffectMode::Vibration);
        assert(cmd.right.frequency_hz == 38);
        assert(cmd.right.amplitude >= 1);
    }
    {
        // Unhealthy scale binding keeps base force — fail-closed.
        BindingValues values{};
        const HidOutputCommand cmd = ComposeCommand(manifest, bow, values, ini);
        assert(cmd.right.force == 3);
        assert(cmd.right_mode == TriggerEffectMode::BowElasticScaled);
    }

    // ---- ComposeCommand: lighting rules -----------------------------------
    {
        BindingValues values{{"player_health_ratio", 1.0},
                             {"adrenaline_segments", 3.0}};
        const HidOutputCommand cmd = ComposeCommand(manifest, bow, values, ini);
        assert(cmd.lightbar_rgb[0] == 20 && cmd.lightbar_rgb[1] == 210 &&
               cmd.lightbar_rgb[2] == 80);  // full health -> high color
        assert(cmd.player_leds == 0b00111);  // 3 adrenaline segments
    }
    {
        // Low health + full adrenaline pulse.
        BindingValues values{{"player_health_ratio", 0.0},
                             {"adrenaline_segments", 5.0}};
        const HidOutputCommand cmd = ComposeCommand(manifest, bow, values, ini);
        assert(cmd.lightbar_rgb[0] == 255 && cmd.lightbar_rgb[1] == 195 &&
               cmd.lightbar_rgb[2] == 0);
        assert(cmd.player_leds == 0b11111);
    }
    {
        // Missing health binding: lightbar untouched (0,0,0), LEDs neutral.
        BindingValues values{};
        const HidOutputCommand cmd = ComposeCommand(manifest, bow, values, ini);
        assert(cmd.lightbar_rgb[0] == 0 && cmd.player_leds == 0);
    }

    // ---- HidController: UDP fallback pipeline with dedup ------------------
    SOCKET listener = INVALID_SOCKET;
    const std::uint16_t port = BindListener(&listener);
    assert(port != 0);

    HidController::Settings settings;
    settings.udp_port = port;
    HidController controller(settings);
    controller.SetIntensityPresets(/*left=*/3, /*right=*/2);  // 1.0x / 0.75x

    HidOutputCommand command;
    command.left_mode = TriggerEffectMode::Resistance;
    command.left = {.start_position = 0, .force = 8};
    command.right_mode = TriggerEffectMode::BowElasticScaled;
    command.right = {.start_position = 1, .end_position = 8, .force = 4,
                     .snap_force = 8};

    assert(controller.Apply(command));  // no DualSense -> DSX UDP fallback
    assert(controller.active_transport() == HidController::Transport::UdpDsx);

    // Four packets: left trigger, right trigger, RGB, player LEDs.
    // Left preset 3 (1.0x): Resistance(0,8) -> [0,1,13,0,8].
    assert(ReceiveJson(listener) ==
           R"({"instructions":[{"parameters":[0,1,13,0,8],"type":1}]})");
    // Right preset 2 (0.75x): force round(4*0.75)=3, snap round(8*0.75)=6.
    assert(ReceiveJson(listener) ==
           R"({"instructions":[{"parameters":[0,2,14,1,8,3,6],"type":1}]})");
    assert(!ReceiveJson(listener).empty());  // RGB
    assert(!ReceiveJson(listener).empty());  // player LEDs
    assert(ReceiveJson(listener).empty());   // no more packets

    // Dedup: identical command must not emit anything new.
    assert(controller.Apply(command));
    assert(ReceiveJson(listener).empty());

    // Left preset 0 (Off) turns the left trigger into Normal.
    controller.SetIntensityPresets(0, 3);
    assert(controller.Apply(command));
    assert(ReceiveJson(listener) ==
           R"({"instructions":[{"parameters":[0,1,0],"type":1}]})");
    assert(!ReceiveJson(listener).empty());  // right now at 1.0x

    closesocket(listener);
    WSACleanup();
    return 0;
}

// test_hid_report.cpp — byte-exact layout assertions for the USB 0x02 /
// BT 0x31 output reports and the adaptive trigger encodings (SF-4).
//
// Expected trigger blocks were hand-computed from the dualsensitive
// profile math (30-bit force tables, position masks), and the BT CRC is
// asserted for EQUIVALENCE against the vendored __DS5W::CRC32::compute.

#include "test_common.hpp"

#include <cstring>
#include <string>
#include <vector>

#include <DS_CRC32.h>

#include "hid_report_builder.hpp"

using namespace senseforge;

namespace {

std::string Hex(const std::uint8_t* data, size_t len) {
    std::string out;
    char buf[4];
    for (size_t i = 0; i < len; ++i) {
        snprintf(buf, sizeof(buf), "%02X ", data[i]);
        out += buf;
    }
    return out;
}

}  // namespace

int main() {
    // ---- Trigger encodings (hand-computed ground truth) ------------------
    std::uint8_t block[11];

    // Off: all zero.
    EncodeTriggerBlock(TriggerEffectMode::Off, {}, block);
    for (int i = 0; i < 11; ++i) assert(block[i] == 0);

    // Resistance(start=0, force=8): b=7 everywhere -> num=0x3FFFFFFF,
    // num2=0x3FF -> {0x21, FF, 03, FF, FF, FF, 3F, 0,0,0,0}
    EncodeTriggerBlock(TriggerEffectMode::Resistance,
                       {.start_position = 0, .force = 8}, block);
    assert(block[0] == 0x21);
    assert(block[1] == 0xFF && block[2] == 0x03);
    assert(block[3] == 0xFF && block[4] == 0xFF && block[5] == 0xFF &&
           block[6] == 0x3F);
    assert(block[7] == 0 && block[10] == 0);

    // Resistance(start=2, force=1): b=0 -> num=0, num2=bits2..9=0x3FC.
    EncodeTriggerBlock(TriggerEffectMode::Resistance,
                       {.start_position = 2, .force = 1}, block);
    assert(block[0] == 0x21);
    assert(block[1] == 0xFC && block[2] == 0x03);
    assert(block[3] == 0 && block[6] == 0);

    // Invalid force (0 / 9) degrades to Off — fail-closed.
    EncodeTriggerBlock(TriggerEffectMode::Resistance,
                       {.start_position = 0, .force = 0}, block);
    assert(block[0] == 0x00);
    EncodeTriggerBlock(TriggerEffectMode::Resistance,
                       {.start_position = 0, .force = 9}, block);
    assert(block[0] == 0x00);

    // RigidSection(start=2, end=5, force=6): Weapon encoding —
    // mask=(1<<2)|(1<<5)=0x24, [3]=force-1=5.
    EncodeTriggerBlock(TriggerEffectMode::RigidSection,
                       {.start_position = 2, .end_position = 5, .force = 6},
                       block);
    assert(block[0] == 0x25);
    assert(block[1] == 0x24 && block[2] == 0x00 && block[3] == 5);

    // Bow(start=1, end=8, force=3, snap=8):
    // mask=(1<<1)|(1<<8)=0x102; pair=((3-1)&7)|(((8-1)&7)<<3)=0x3A.
    EncodeTriggerBlock(TriggerEffectMode::BowElasticScaled,
                       {.start_position = 1, .end_position = 8, .force = 3,
                        .snap_force = 8},
                       block);
    assert(block[0] == 0x22);
    assert(block[1] == 0x02 && block[2] == 0x01);
    assert(block[3] == 0x3A && block[4] == 0x00);

    // Vibration(start=0, amplitude=8, freq=38): table b=7 + freq at [9].
    EncodeTriggerBlock(TriggerEffectMode::Vibration,
                       {.start_position = 0, .amplitude = 8, .frequency_hz = 38},
                       block);
    assert(block[0] == 0x26);
    assert(block[3] == 0xFF && block[4] == 0xFF && block[5] == 0xFF &&
           block[6] == 0x3F);
    assert(block[9] == 38);

    // ---- USB report layout (spec §2.1 / dualsensitive framing) -----------
    HidOutputCommand command;
    command.left_mode = TriggerEffectMode::Resistance;
    command.left = {.start_position = 0, .force = 8};
    command.right_mode = TriggerEffectMode::BowElasticScaled;
    command.right = {.start_position = 1, .end_position = 8, .force = 3,
                     .snap_force = 8};
    command.lightbar_rgb[0] = 20;
    command.lightbar_rgb[1] = 210;
    command.lightbar_rgb[2] = 80;
    command.player_leds = 0b01111;
    command.player_led_brightness = 1;

    std::uint8_t usb[48];
    BuildUsbReport(command, usb);

    assert(usb[0x00] == 0x02);  // report id
    assert(usb[0x01] == 0xFF && usb[0x02] == 0xF7);  // feature mask
    assert(usb[0x03] == 0 && usb[0x04] == 0);        // rumble suppressed
    assert(usb[0x27] == 0x03);                       // dualsensitive constant
    // SF-12b: speaker enabled (volume + output path + preamp), never muted
    assert(usb[0x06] == 0x64 && usb[0x08] == 0x30 && usb[0x26] == 0x02);
    assert(usb[0x2A] == 0x02);                       // leds enabled
    assert(usb[0x2B] == 1);                          // medium brightness
    assert(usb[0x2C] == (0b01111 | 0x20));           // player LEDs + no-fade
    assert(usb[0x2D] == 20 && usb[0x2E] == 210 && usb[0x2F] == 80);

    // Trigger blocks land at the ground-truth offsets: right 0x0B, left 0x16.
    std::uint8_t right_expected[11];
    std::uint8_t left_expected[11];
    EncodeTriggerBlock(command.right_mode, command.right, right_expected);
    EncodeTriggerBlock(command.left_mode, command.left, left_expected);
    assert(std::memcmp(&usb[0x0B], right_expected, 11) == 0);
    assert(std::memcmp(&usb[0x16], left_expected, 11) == 0);

    // ---- BT report layout -------------------------------------------------
    std::uint8_t bt[78];
    BuildBtReport(command, bt);
    assert(bt[0x00] == 0x31);   // report id
    assert(bt[0x01] == 0x02);   // seq constant (dualsensitive)
    assert(bt[0x02] == 0xFF && bt[0x03] == 0xF7);
    // Payload shifted by 2: right trigger at 0x0C, left at 0x17.
    assert(std::memcmp(&bt[0x0C], right_expected, 11) == 0);
    assert(std::memcmp(&bt[0x17], left_expected, 11) == 0);
    assert(bt[0x2D] == (0b01111 | 0x20));  // player LEDs (payload 0x2B + 2)
    assert(bt[0x2E] == 20 && bt[0x2F] == 210 && bt[0x30] == 80);

    // CRC bytes at 0x4A..0x4D and equivalence with the vendored CRC32.
    const std::uint32_t stored = bt[0x4A] | (bt[0x4B] << 8) | (bt[0x4C] << 16) |
                                 (static_cast<std::uint32_t>(bt[0x4D]) << 24);
    assert(stored == Crc32Sony(bt, 74));
    assert(Crc32Sony(bt, 74) == __DS5W::CRC32::compute(bt, 74));

    // Empty input returns the seed (update loop never runs).
    assert(Crc32Sony(nullptr, 0) == 0xeada2d49);

    // ---- Rumble motors (SF-12b) -------------------------------------------
    {
        HidOutputCommand rumbly;
        rumbly.motor_left = 200;   // hard motor
        rumbly.motor_right = 100;  // soft motor
        std::uint8_t r[48];
        BuildUsbReport(rumbly, r);
        // dualsensitive payload: [2]=rightRumble -> report 0x03,
        // [3]=leftRumble -> report 0x04.
        assert(r[0x03] == 100);
        assert(r[0x04] == 200);
        std::uint8_t rb[78];
        BuildBtReport(rumbly, rb);
        // usb[i] lands at bt[i+1] (payload copy from usb[1] to bt[2]).
        assert(rb[0x04] == 100 && rb[0x05] == 200);
    }

    return 0;
}

// hid_report_builder.cpp — encodings byte-identical to the dualsensitive
// profiles (MIT, tpetsas: src/dualsensitive.cpp setTriggerProfile), read
// and verified against the vendored sources during SF-4.

#include "hid_report_builder.hpp"

#include <algorithm>
#include <array>
#include <cstring>

namespace senseforge {
namespace {

inline bool ValidForce(int f) {
    return f >= 1 && f <= 8;
}

// Rigid_A "30-bit force table": each position i in [start, 10) carries the
// 3-bit force (force-1) plus a bitmask of the active positions.
void EncodeForceTable(std::uint8_t out[11], int start, int force) {
    const std::uint8_t b = static_cast<std::uint8_t>((force - 1) & 7);
    std::uint32_t num = 0;
    std::uint16_t num2 = 0;
    for (int i = start; i < 10; ++i) {
        num |= static_cast<std::uint32_t>(b) << (3 * i);
        num2 |= static_cast<std::uint16_t>(1 << i);
    }
    out[1] = static_cast<std::uint8_t>(num2 & 0xFF);
    out[2] = static_cast<std::uint8_t>((num2 >> 8) & 0xFF);
    out[3] = static_cast<std::uint8_t>(num & 0xFF);
    out[4] = static_cast<std::uint8_t>((num >> 8) & 0xFF);
    out[5] = static_cast<std::uint8_t>((num >> 16) & 0xFF);
    out[6] = static_cast<std::uint8_t>((num >> 24) & 0xFF);
}

}  // namespace

void EncodeTriggerBlock(TriggerEffectMode mode, const TriggerParams& params,
                        std::uint8_t out[11]) {
    std::memset(out, 0, 11);

    const int start = std::clamp(params.start_position, 0, 9);
    const int end = std::clamp(params.end_position, 0, 9);

    switch (mode) {
        case TriggerEffectMode::Resistance: {
            if (!ValidForce(params.force)) {
                break;  // invalid -> Off, fail-closed
            }
            out[0] = 0x21;  // Rigid_A
            EncodeForceTable(out, start, params.force);
            break;
        }

        case TriggerEffectMode::RigidSection:
        case TriggerEffectMode::WeaponSnap: {
            // Weapon profile (dualsensitive): wall between start/end.
            if (start < 2 || start > 7 || end <= start || end > 8 ||
                !ValidForce(params.force)) {
                break;
            }
            out[0] = 0x25;  // Rigid_AB
            const std::uint16_t num =
                static_cast<std::uint16_t>((1 << start) | (1 << end));
            out[1] = static_cast<std::uint8_t>(num & 0xFF);
            out[2] = static_cast<std::uint8_t>((num >> 8) & 0xFF);
            out[3] = static_cast<std::uint8_t>(params.force - 1);
            break;
        }

        case TriggerEffectMode::BowElasticScaled: {
            // Bow profile (dualsensitive): position mask + force/snap pair.
            if (start > 8 || end > 8 || start >= end ||
                !ValidForce(params.force) ||
                !ValidForce(params.snap_force)) {
                break;
            }
            out[0] = 0x22;  // Pulse_A
            const std::uint16_t num =
                static_cast<std::uint16_t>((1 << start) | (1 << end));
            const std::uint32_t num2 =
                static_cast<std::uint32_t>(((params.force - 1) & 7) |
                                            (((params.snap_force - 1) & 7) << 3));
            out[1] = static_cast<std::uint8_t>(num & 0xFF);
            out[2] = static_cast<std::uint8_t>((num >> 8) & 0xFF);
            out[3] = static_cast<std::uint8_t>(num2 & 0xFF);
            out[4] = static_cast<std::uint8_t>((num2 >> 8) & 0xFF);
            break;
        }

        case TriggerEffectMode::Vibration: {
            // Vibration profile (dualsensitive): force table + frequency [9].
            if (params.amplitude < 1 || params.amplitude > 8 ||
                params.frequency_hz < 1) {
                break;
            }
            out[0] = 0x26;
            EncodeForceTable(out, start, params.amplitude);
            out[9] = static_cast<std::uint8_t>(
                std::clamp(params.frequency_hz, 1, 255));
            break;
        }

        case TriggerEffectMode::Off:
        default:
            break;  // 0x00 — no resistance
    }
}

void BuildUsbReport(const HidOutputCommand& command,
                    std::uint8_t out[kUsbReportSize]) {
    std::memset(out, 0, kUsbReportSize);

    out[0x00] = 0x02;   // report id (USB)
    out[0x01] = 0xFF;   // feature mask (dualsensitive createHidOutputBuffer)
    out[0x02] = 0xF7;
    // Rumble motors (dualsensitive payload: [2]=rightRumble, [3]=leftRumble
    // -> report [0x03]/[0x04]). Zero when HD Haptics owns feedback (SF-5);
    // event flashes (SF-12b) drive them transiently.
    out[0x03] = command.motor_right;
    out[0x04] = command.motor_left;
    // SF-12b: controller speaker. The valid flags above (0x20 speaker volume,
    // 0x80 audio control, flag1 0x80 audio control 2) were set while these
    // fields stayed 0, which explicitly MUTED the speaker on every report
    // (debrief 25/10 PID 30472: item_pickup tone reached the mixer, nothing
    // came out). Values as in the Linux hid-playstation driver for "no
    // headphones": output path 0x30 (speaker), volume 0x64 (accepted range
    // 0x3D..0x64), preamp +6dB. The mixer's speaker_volume scales the level.
    out[0x06] = 0x64;   // speaker volume
    out[0x08] = 0x30;   // audio control: output path = speaker
    out[0x26] = 0x02;   // audio control 2: speaker preamp gain +6dB
    // [0x09] mic led stays 0 (Off).

    EncodeTriggerBlock(command.right_mode, command.right, &out[0x0B]);
    EncodeTriggerBlock(command.left_mode, command.left, &out[0x16]);

    out[0x27] = 0x03;  // constant written by dualsensitive
    out[0x2A] = command.disable_leds ? 0x01 : 0x02;
    out[0x2B] = static_cast<std::uint8_t>(
        std::clamp(command.player_led_brightness, 0, 2));
    // playerLedFade = false -> bit 0x20 set (dualsensitive semantics).
    out[0x2C] = static_cast<std::uint8_t>(command.player_leds | 0x20);
    out[0x2D] = command.lightbar_rgb[0];
    out[0x2E] = command.lightbar_rgb[1];
    out[0x2F] = command.lightbar_rgb[2];
}

void BuildBtReport(const HidOutputCommand& command,
                   std::uint8_t out[kBtReportSize]) {
    std::memset(out, 0, kBtReportSize);

    out[0x00] = 0x31;  // report id (Bluetooth)
    out[0x01] = 0x02;  // constant seq byte, as shipped by dualsensitive

    // Same payload, shifted by the extra BT header byte.
    std::uint8_t usb[kUsbReportSize];
    BuildUsbReport(command, usb);
    std::memcpy(&out[0x02], &usb[0x01], kUsbReportSize - 1);

    const std::uint32_t crc = Crc32Sony(out, 74);
    out[0x4A] = static_cast<std::uint8_t>(crc & 0xFF);
    out[0x4B] = static_cast<std::uint8_t>((crc >> 8) & 0xFF);
    out[0x4C] = static_cast<std::uint8_t>((crc >> 16) & 0xFF);
    out[0x4D] = static_cast<std::uint8_t>((crc >> 24) & 0xFF);
}

std::uint32_t Crc32Sony(const std::uint8_t* data, std::size_t len) {
    // dualsensitive DS_CRC32.cpp: standard reflected CRC-32 table with every
    // entry XORed by 0xd202ef8d, seed 0xeada2d49, update
    // result = table[(result ^ byte) & 0xFF] ^ (result >> 8), no final XOR.
    static constexpr std::uint32_t kXorMask = 0xd202ef8d;
    static const std::array<std::uint32_t, 256> table = [] {
        std::array<std::uint32_t, 256> t{};
        for (std::uint32_t i = 0; i < 256; ++i) {
            std::uint32_t c = i;
            for (int k = 0; k < 8; ++k) {
                c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
            }
            t[i] = c ^ kXorMask;
        }
        return t;
    }();

    std::uint32_t result = 0xeada2d49;  // BTFW CRC seed
    for (std::size_t i = 0; i < len; ++i) {
        result = table[(result ^ data[i]) & 0xFF] ^ (result >> 8);
    }
    return result;
}

}  // namespace senseforge

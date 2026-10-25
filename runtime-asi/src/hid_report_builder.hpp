// hid_report_builder.hpp — byte-exact DualSense output reports (SF-4).
//
// Layout grounded on the dualsensitive core (MIT, tpetsas / Ohjurot
// DualSense-Windows), read directly from third_party/dualsensitive:
//   src/core/DS5_Output.cpp (payload offsets) + src/core/IO.cpp (framing).
//
// USB report (48 bytes): [0]=0x02, payload at [1..47]:
//   [1]=0xFF [2]=0xF7 feature mask · [3]/[4] rumble R/L · [9] mic led
//   [0x0B..0x15] RIGHT trigger block · [0x16..0x20] LEFT trigger block
//   [0x27]=0x03 · [0x2A] disable leds · [0x2B] player-LED brightness
//   [0x2C] player LEDs · [0x2D..0x2F] lightbar RGB
//
// BT report (78 bytes): [0]=0x31 [1]=seq · payload at [2..] (shift +2)
//   · CRC32 (Sony variant) of [0..73] stored at [0x4A..0x4D].

#pragma once

#include <cstddef>
#include <cstdint>

namespace senseforge {

// Manifest trigger modes, mapped to native DualSense effect encodings:
//   Off               -> 0x00 (no resistance)
//   Resistance        -> 0x21 Rigid_A   (constant force from start, 30-bit table)
//   RigidSection      -> 0x25 Rigid_AB  (hard wall between start/end)      [Weapon]
//   WeaponSnap        -> 0x25 Rigid_AB  (same encoding, snap semantics)    [Weapon]
//   BowElasticScaled  -> 0x22 Pulse_A   (position mask + force/snap pair)  [Bow]
//   Vibration         -> 0x26           (30-bit table + frequency at [9]) [Vibration]
enum class TriggerEffectMode {
    Off,
    Resistance,
    RigidSection,
    WeaponSnap,
    BowElasticScaled,
    Vibration,
};

struct TriggerParams {
    int start_position = 0;  // 0-9 (DualSense position units)
    int end_position = 0;     // 0-9
    int force = 0;           // 1-8 (resistance / bow base force)
    int snap_force = 0;       // 1-8 (bow snap / section wall force)
    int amplitude = 0;        // 1-8 (vibration)
    int frequency_hz = 0;     // >= 1 (vibration frequency, byte-clamped)
};

// Encodes one 11-byte adaptive trigger block. Invalid parameters degrade to
// Off (0x00) — fail-closed, the game never receives a malformed effect.
void EncodeTriggerBlock(TriggerEffectMode mode, const TriggerParams& params,
                        std::uint8_t out[11]);

struct HidOutputCommand {
    TriggerEffectMode left_mode = TriggerEffectMode::Off;
    TriggerEffectMode right_mode = TriggerEffectMode::Off;
    TriggerParams left;
    TriggerParams right;
    std::uint8_t lightbar_rgb[3] = {0, 0, 0};
    std::uint8_t player_leds = 0;   // bitmask, 5 LEDs (0b00011111)
    int player_led_brightness = 1;  // 0=High 1=Medium 2=Low (DS5W LedBrightness)
    bool disable_leds = false;
    // Rumble motors (SF-12b: parry/dodge feedback). Direct HID only —
    // the DSX UDP v1 protocol has no rumble instruction.
    std::uint8_t motor_left = 0;   // hard rumble 0-255
    std::uint8_t motor_right = 0;  // soft rumble 0-255
};

constexpr std::size_t kUsbReportSize = 48;
constexpr std::size_t kBtReportSize = 78;

// Fills a full USB output report (report id 0x02). Zero-initialized first.
void BuildUsbReport(const HidOutputCommand& command,
                    std::uint8_t out[kUsbReportSize]);

// Fills a full Bluetooth output report (report id 0x31 + Sony CRC32 of the
// first 74 bytes stored at [0x4A..0x4D]). Zero-initialized first.
void BuildBtReport(const HidOutputCommand& command,
                    std::uint8_t out[kBtReportSize]);

// Sony/BT CRC32 as implemented by the dualsensitive core (DS_CRC32.cpp):
// standard reflected table (poly 0xEDB88320) with every entry XORed with
// 0xd202ef8d, seed 0xeada2d49, no final XOR. Mirrored exactly so our
// builder's BT frames match what the integration target produces.
std::uint32_t Crc32Sony(const std::uint8_t* data, std::size_t len);

}  // namespace senseforge

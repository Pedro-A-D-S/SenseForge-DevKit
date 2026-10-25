// udp_dsx_client.hpp — DSX/DualSenseY-v2 UDP fallback transport (SF-4).
//
// Wire format grounded on the official DualSenseX UDP example (C# v2.0,
// Paliverse/DualSenseX: Resources.cs + Program.cs): JSON packets to
// 127.0.0.1:<port> (DSX default 6969) of the shape
//   {"instructions":[{"type":1,"parameters":[0, 1, 13, 0, 8]}]}
// with InstructionType/Trigger/TriggerMode serialized as NUMERIC enums
// (Newtonsoft default). Best-effort: no connection state, no blocking.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace senseforge {

// InstructionType (DSX official example enum)
enum class DsxInstruction {
    TriggerUpdate = 1,
    RgbUpdate = 2,
    PlayerLed = 3,
    TriggerThreshold = 4,
    MicLed = 5,
    PlayerLedNewRevision = 6,
    ResetToUserSettings = 7,
};

// TriggerMode (DSX official example enum) — only the ones we emit.
enum class DsxTriggerMode {
    Normal = 0,
    Rigid = 7,
    VibrateTrigger = 8,
    Resistance = 13,
    Bow = 14,
    SemiAutomaticGun = 16,  // start/end wall — used for rigid_section/weapon_snap
};

class UdpDsxClient {
public:
    explicit UdpDsxClient(std::uint16_t port = 6969,
                          std::string host = "127.0.0.1");
    ~UdpDsxClient();

    UdpDsxClient(const UdpDsxClient&) = delete;
    UdpDsxClient& operator=(const UdpDsxClient&) = delete;

    // Sends one JSON packet. Returns false on socket failure (DSX not
    // running) — never throws, never blocks long.
    bool Send(const std::string& json_packet);

    // ---- Packet builders (pure functions — unit-testable) ----------------

    // TriggerUpdate: [controllerIndex, 1=Left|2=Right, mode, ...modeParams]
    static std::string BuildTriggerJson(int controller_index, bool right,
                                        DsxTriggerMode mode,
                                        const std::vector<int>& mode_params);

    // RGBUpdate: [controllerIndex, r, g, b]
    static std::string BuildRgbJson(int controller_index, std::uint8_t r,
                                    std::uint8_t g, std::uint8_t b);

    // PlayerLED: [controllerIndex, led1..led5 (bools)]
    static std::string BuildPlayerLedJson(int controller_index,
                                          std::uint8_t led_bitmask);

    // Low-level: {"instructions":[...]} with raw type + parameters.
    static std::string BuildPacket(DsxInstruction type,
                                   const std::vector<int>& parameters);

private:
    bool EnsureSocket();

    std::string host_;
    std::uint16_t port_;
    void* socket_ = nullptr;  // SOCKET, kept opaque to avoid winsock leak
};

}  // namespace senseforge

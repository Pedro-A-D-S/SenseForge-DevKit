// memory_scanner.hpp — SEH fail-closed pointer-chain reader (SF-3).
//
// SENSEFORGE_SPEC.md §2.1: resolves BaseAddress + RVA + Offsets chains
// defined in {game}.sense.json every frame (60–120Hz); any null pointer or
// access violation during loading screens must never crash the game.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace senseforge {

enum class BindingType { UInt8, UInt32, Float32 };

struct BindingValue {
    BindingType type = BindingType::UInt8;
    union {
        std::uint8_t u8;
        std::uint32_t u32;
        float f32;
    };

    double AsDouble() const {
        switch (type) {
            case BindingType::UInt32:
                return static_cast<double>(u32);
            case BindingType::Float32:
                return static_cast<double>(f32);
            case BindingType::UInt8:
            default:
                return static_cast<double>(u8);
        }
    }
};

// Mirrors one entry of manifest "memory_Bindings" (spec §3.1).
struct PointerBinding {
    std::string module;  // e.g. "ACOdyssey.exe"
    std::uintptr_t base_rva = 0;
    std::vector<std::uintptr_t> pointer_offsets;  // last offset = data field
    BindingType type = BindingType::UInt8;
    bool has_expected_range = false;
    double range_min = 0.0;
    double range_max = 0.0;
};

class MemoryScanner {
public:
    // Fail-closed read: false on missing module, AV anywhere in the chain,
    // null intermediate pointer, or value outside expected_range.
    bool Read(const PointerBinding& binding, BindingValue& out) const;

    // Exposed for unit tests against synthetic in-process structures.
    static bool ResolveChain(std::uintptr_t first,
                             const std::uintptr_t* offsets, size_t count,
                             std::uintptr_t& out_address);
    static bool ReadTyped(std::uintptr_t address, BindingType type,
                          BindingValue& out);
};

}  // namespace senseforge

// memory_scanner.cpp — SEH implementation (SF-3).
//
// MSVC forbids __try in functions that require C++ unwinding (C2712), so the
// raw pointer walks are POD-only helpers and the public API stays plain C++.

#include "memory_scanner.hpp"

#include <windows.h>

namespace senseforge {
namespace {

// Walks base -> *(+off[0]) -> ... -> address + off[n-1].
bool TryWalkChain(std::uintptr_t first, const std::uintptr_t* offsets,
                  size_t count, std::uintptr_t* out_address) {
    __try {
        std::uintptr_t addr = first;
        for (size_t i = 0; i + 1 < count; ++i) {
            if (addr == 0) {
                return false;
            }
            addr = *reinterpret_cast<std::uintptr_t*>(addr + offsets[i]);
            if (addr == 0) {
                return false;  // null link — loading screen / object teardown
            }
        }
        *out_address = (count == 0) ? addr : addr + offsets[count - 1];
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;  // access violation — fail-closed, never crash the game
    }
}

bool TryReadValue(std::uintptr_t address, BindingType type, void* out) {
    __try {
        switch (type) {
            case BindingType::UInt8:
                *static_cast<std::uint8_t*>(out) =
                    *reinterpret_cast<const std::uint8_t*>(address);
                return true;
            case BindingType::UInt32:
                *static_cast<std::uint32_t*>(out) =
                    *reinterpret_cast<const std::uint32_t*>(address);
                return true;
            case BindingType::Float32:
                *static_cast<float*>(out) =
                    *reinterpret_cast<const float*>(address);
                return true;
        }
        return false;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

}  // namespace

bool MemoryScanner::ResolveChain(std::uintptr_t first,
                                  const std::uintptr_t* offsets, size_t count,
                                  std::uintptr_t& out_address) {
    std::uintptr_t resolved = 0;
    if (!TryWalkChain(first, offsets, count, &resolved)) {
        return false;
    }
    out_address = resolved;
    return true;
}

bool MemoryScanner::ReadTyped(std::uintptr_t address, BindingType type,
                              BindingValue& out) {
    BindingValue value{};
    value.type = type;
    if (!TryReadValue(address, type, &value.u32)) {
        return false;
    }
    // u32 aliases the union: the per-type stores above filled the right slot.
    out = value;
    return true;
}

bool MemoryScanner::Read(const PointerBinding& binding,
                         BindingValue& out) const {
    const HMODULE module = GetModuleHandleA(binding.module.c_str());
    if (module == nullptr) {
        return false;
    }
    const std::uintptr_t first =
        reinterpret_cast<std::uintptr_t>(module) + binding.base_rva;

    std::uintptr_t address = 0;
    if (!ResolveChain(first, binding.pointer_offsets.data(),
                      binding.pointer_offsets.size(), address)) {
        return false;
    }

    BindingValue value{};
    if (!ReadTyped(address, binding.type, value)) {
        return false;
    }

    if (binding.has_expected_range) {
        const double d = value.AsDouble();
        if (d < binding.range_min || d > binding.range_max) {
            return false;  // suspicious value — treat as fail-closed
        }
    }

    out = value;
    return true;
}

}  // namespace senseforge

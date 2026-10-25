// test_memory_scanner.cpp — SEH fail-closed chain resolution (SF-3).
// Builds synthetic multi-level chains in this process' own memory and
// verifies: correct resolution, null-link rejection, access-violation
// rejection (no crash), and expected_range enforcement.

#include "test_common.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <windows.h>

#include "memory_scanner.hpp"

namespace {

struct Inner {
    std::uint8_t flags = 1;
    std::uint32_t counter = 0x12345678;
    float ratio = 0.5f;
};

struct Outer {
    Inner* inner = nullptr;
};

Inner g_inner{};
Outer g_outer{&g_inner};

}  // namespace

int main() {
    using senseforge::BindingValue;
    using senseforge::MemoryScanner;
    using senseforge::PointerBinding;

    // --- Chain resolution against a synthetic structure ------------------
    const std::uintptr_t inner_addr = reinterpret_cast<std::uintptr_t>(&g_inner);
    const std::uintptr_t offsets[] = {offsetof(Outer, inner),
                                      offsetof(Inner, counter)};
    std::uintptr_t resolved = 0;
    assert(MemoryScanner::ResolveChain(reinterpret_cast<std::uintptr_t>(&g_outer),
                                        offsets, 2, resolved));
    assert(resolved ==
           inner_addr + offsetof(Inner, counter));  // last offset = data

    BindingValue value;
    assert(MemoryScanner::ReadTyped(resolved,
                                     senseforge::BindingType::UInt32, value));
    assert(value.u32 == 0x12345678);

    assert(MemoryScanner::ReadTyped(inner_addr + offsetof(Inner, ratio),
                                     senseforge::BindingType::Float32, value));
    assert(value.f32 == 0.5f);

    // --- Null intermediate link: fail-closed, no dereference --------------
    Outer broken{nullptr};
    const std::uintptr_t broken_offsets[] = {offsetof(Outer, inner),
                                             offsetof(Inner, counter)};
    assert(!MemoryScanner::ResolveChain(
        reinterpret_cast<std::uintptr_t>(&broken), broken_offsets, 2,
        resolved));

    // --- Access violation: SEH must swallow it and return false ----------
    // 0xFFFF'DEAD'BEEF'0000 is canonical kernel space — dereferencing it
    // from user mode faults. Note a 1-offset chain never dereferences (the
    // single offset is the data field), so the poison walk needs two links.
    const std::uintptr_t poison[] = {0x10, 0x0};
    assert(!MemoryScanner::ResolveChain(0xFFFFDEADBEEF0000ULL, poison, 2,
                                        resolved));
    assert(!MemoryScanner::ReadTyped(0xFFFFDEADBEEF0000ULL,
                                      senseforge::BindingType::UInt8, value));

    // --- Full Read() against this module's own globals -------------------
    const HMODULE self = GetModuleHandleA(nullptr);
    const auto rva_of = [&](const void* p) {
        return reinterpret_cast<std::uintptr_t>(p) -
               reinterpret_cast<std::uintptr_t>(self);
    };

    // The scanner resolves modules by their loaded name — use the real
    // filename of this test executable (whatever the test runner called it).
    wchar_t self_path[MAX_PATH]{};
    GetModuleFileNameW(nullptr, self_path, MAX_PATH);
    const std::wstring self_name =
        std::filesystem::path(self_path).filename().wstring();
    std::string module_name(self_name.size(), '\0');
    WideCharToMultiByte(CP_ACP, 0, self_name.c_str(),
                       static_cast<int>(self_name.size()), module_name.data(),
                       static_cast<int>(module_name.size()), nullptr, nullptr);

    PointerBinding binding;
    binding.module = module_name;
    binding.base_rva = rva_of(&g_outer);
    binding.pointer_offsets = {offsetof(Outer, inner), offsetof(Inner, flags)};
    binding.type = senseforge::BindingType::UInt8;
    binding.has_expected_range = true;
    binding.range_min = 0;
    binding.range_max = 1;

    MemoryScanner scanner;
    BindingValue menu_flag;
    assert(scanner.Read(binding, menu_flag));
    assert(menu_flag.u8 == 1);

    // Out-of-expected-range ⇒ fail-closed (suspicious pointer: refuse).
    g_inner.flags = 7;
    assert(!scanner.Read(binding, menu_flag));
    g_inner.flags = 1;

    // Unknown module ⇒ fail-closed.
    PointerBinding missing_module = binding;
    missing_module.module = "no_such_module_9f21.dll";
    assert(!scanner.Read(missing_module, menu_flag));

    return 0;
}

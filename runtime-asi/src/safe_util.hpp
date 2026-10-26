// safe_util.hpp — small hardening helpers shared by the runtime (SF-22).
//
// Header-only and free of Windows dependencies so the contract is
// unit-testable. Everything that turns an untrusted double into an integer
// type, or an untrusted string into a path, goes through here:
//   * a double outside the target type's range (or NaN) is undefined
//     behaviour in a plain static_cast;
//   * a manifest/ini path such as "..\\..\\x.wav" or "C:\\x.wav" must not
//     escape the game directory.

#pragma once

#include <cmath>
#include <cstdint>
#include <filesystem>

namespace senseforge {

// 0..255, rounded to nearest; NaN and anything <= 0 give 0.
inline std::uint8_t ClampByte(double value) {
    if (!(value > 0.0)) {
        return 0;  // also catches NaN
    }
    if (value >= 255.0) {
        return 255;
    }
    return static_cast<std::uint8_t>(value + 0.5);
}

// lo..hi, rounded to nearest; NaN gives lo. Requires lo <= hi.
inline int ClampInt(double value, int lo, int hi) {
    if (std::isnan(value) || value <= static_cast<double>(lo)) {
        return lo;
    }
    if (value >= static_cast<double>(hi)) {
        return hi;
    }
    const int rounded = static_cast<int>(value + 0.5);
    return rounded > hi ? hi : rounded;
}

// Resolves `relative` under `base` and refuses anything that leaves `base`:
// empty or absolute paths, drive/root names, and ".." that climbs out
// (symlinks in existing components are resolved before the comparison).
// On success `*out` is the joined path; on failure it is left untouched.
inline bool ResolveInside(const std::filesystem::path& base,
                          const std::filesystem::path& relative,
                          std::filesystem::path* out) {
    if (relative.empty() || relative.is_absolute() ||
        relative.has_root_name() || relative.has_root_directory()) {
        return false;
    }
    std::error_code ec;
    const auto base_canonical = std::filesystem::weakly_canonical(base, ec);
    if (ec) {
        return false;
    }
    const auto joined = std::filesystem::weakly_canonical(base / relative, ec);
    if (ec) {
        return false;
    }
    const auto inside = joined.lexically_relative(base_canonical);
    // "." is `base` itself (also what "x/.." resolves to): not a file inside.
    if (inside.empty() || inside == "." || *inside.begin() == "..") {
        return false;
    }
    if (out != nullptr) {
        *out = base / relative;
    }
    return true;
}

}  // namespace senseforge

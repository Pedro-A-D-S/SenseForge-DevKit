// SF-1 sanity test — verifies the MSVC toolchain compiles the C++20 feature
// set the runtime depends on, and locks the Soft Limiter contract early
// (SENSEFORGE_SPEC.md §2.2): sample = tanh(sample) * ceiling, per channel.
//
// The runtime ships in Release, so keep assert() active there — the test is
// the contract, it must not compile out under NDEBUG.
#ifdef NDEBUG
#undef NDEBUG
#endif

#include <cassert>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <string_view>

static std::uint32_t soft_limit(float sample, float ceiling) {
    const float limited = std::tanh(sample) * ceiling;
    // Shift into [0, 2000] so both saturation ends are easy to assert.
    return static_cast<std::uint32_t>(limited * 1000.0f) + 1000u;
}

int main() {
    // Identity at zero.
    assert(soft_limit(0.0f, 0.92f) == 1000u);
    // Positive saturation: tanh(+inf) -> +1, scaled by ceiling.
    assert(soft_limit(99.0f, 0.92f) <= 1920u);
    assert(soft_limit(99.0f, 0.92f) > 1910u);
    // Negative saturation: tanh(-inf) -> -1.
    assert(soft_limit(-99.0f, 0.92f) >= 80u);
    assert(soft_limit(-99.0f, 0.92f) < 90u);
    // Monotonic in the linear region.
    assert(soft_limit(0.1f, 0.92f) < soft_limit(0.2f, 0.92f));

    // <filesystem> is available (hot_reload watcher dependency, SF-3).
    const std::filesystem::path profile{"profiles/acodyssey"};
    assert(profile.filename().string() == "acodyssey");

    (void)std::string_view{"senseforge"};
    return 0;
}

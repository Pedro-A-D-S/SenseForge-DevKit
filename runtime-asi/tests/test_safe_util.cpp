// test_safe_util.cpp — the hardening helpers' contract (SF-22): numeric
// clamps must never rely on out-of-range double->integer casts, and path
// resolution must refuse anything that leaves the base directory.

#include "test_common.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>

#include "safe_util.hpp"

using namespace senseforge;
namespace fs = std::filesystem;

int main() {
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();

    // ---- ClampByte --------------------------------------------------------
    assert(ClampByte(0.0) == 0);
    assert(ClampByte(0.4) == 0);
    assert(ClampByte(0.5) == 1);
    assert(ClampByte(127.4) == 127);
    assert(ClampByte(254.6) == 255);
    assert(ClampByte(255.0) == 255);
    assert(ClampByte(1e30) == 255);
    assert(ClampByte(inf) == 255);
    assert(ClampByte(-5.0) == 0);
    assert(ClampByte(-inf) == 0);
    assert(ClampByte(nan) == 0);

    // ---- ClampInt ---------------------------------------------------------
    assert(ClampInt(3.4, 0, 5) == 3);
    assert(ClampInt(3.5, 0, 5) == 4);
    assert(ClampInt(99.0, 0, 5) == 5);
    assert(ClampInt(-99.0, 0, 5) == 0);
    assert(ClampInt(inf, 1, 8) == 8);
    assert(ClampInt(-inf, 1, 8) == 1);
    assert(ClampInt(nan, 1, 8) == 1);
    assert(ClampInt(1e300, 0, 1000) == 1000);
    assert(ClampInt(4.7, 5, 5) == 5);

    // ---- ResolveInside ----------------------------------------------------
    const fs::path base = fs::temp_directory_path() / "sf_safe_util_base";
    const fs::path outside = fs::temp_directory_path() / "sf_safe_util_outside";
    fs::remove_all(base);
    fs::remove_all(outside);
    fs::create_directories(base / "haptics");
    fs::create_directories(outside);
    std::ofstream(outside / "secret.wav") << "x";

    fs::path resolved;
    assert(ResolveInside(base, "haptics/bow.wav", &resolved));
    assert(resolved == base / "haptics/bow.wav");
    assert(ResolveInside(base, "loop.wav", &resolved));
    assert(ResolveInside(base, "haptics/../haptics/bow.wav", nullptr));
    assert(ResolveInside(base, "not_created_yet/file.wav", nullptr));

    fs::path untouched = "keep";
    assert(!ResolveInside(base, "", &untouched));
    assert(!ResolveInside(base, "..", &untouched));
    assert(!ResolveInside(base, "../sf_safe_util_outside/secret.wav", &untouched));
    assert(!ResolveInside(base, "haptics/../../sf_safe_util_outside/secret.wav",
                          &untouched));
    assert(!ResolveInside(base, outside / "secret.wav", &untouched));  // absolute
    assert(!ResolveInside(base, "C:/Windows/System32/x.wav", &untouched));
    assert(!ResolveInside(base, "\\\\server\\share\\x.wav", &untouched));
    assert(!ResolveInside(base, "/rooted/x.wav", &untouched));
    assert(untouched == "keep");  // failure leaves *out alone

    // The base itself is not a file inside it.
    assert(!ResolveInside(base, ".", &untouched));
    assert(!ResolveInside(base, "haptics/..", &untouched));

    // A symlink/junction that points out of the base is refused (best
    // effort: creating one needs privilege on Windows, so skip if refused).
    std::error_code link_ec;
    fs::create_directory_symlink(outside, base / "escape", link_ec);
    if (!link_ec) {
        assert(!ResolveInside(base, "escape/secret.wav", nullptr));
    }

    fs::remove_all(base);
    fs::remove_all(outside);
    return 0;
}

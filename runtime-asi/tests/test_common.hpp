// test_common.hpp — keeps assert() alive in Release builds: these test
// executables ARE the contract (see test_sanity.cpp note, SF-1).

#pragma once
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>

// dualsensitive_compat.h — force-included (/FI) ONLY into the vendored
// dualsensitive_core target. The vendored sources rely on the windows.h
// min/max macros, which our global NOMINMAX definition strips; restore the
// exact classic definitions so the dependency compiles untouched.
#ifndef max
#define max(a, b) (((a) > (b)) ? (a) : (b))
#endif
#ifndef min
#define min(a, b) (((a) < (b)) ? (a) : (b))
#endif

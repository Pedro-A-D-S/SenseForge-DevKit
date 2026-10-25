// sf_debug_logger.hpp — SF-11b crash triage: file event log + minidump on
// unhandled exceptions. Compiled into every runtime flavor (addon/asi/proxy).
// E33 live bisection showed the game dying with fault module =
// senseforge.addon64 (~9-19s post-gate, HID on) and no usable dump: this
// logger gives us the exact crash site and the HID event timeline.
#pragma once

namespace senseforge {

// Opens <LOCALAPPDATA>/senseforge/logs/<exe>_<pid>.log and installs the
// unhandled-exception filter that writes
// <LOCALAPPDATA>/senseforge/crash/<exe>_<pid>.dmp. Idempotent; safe to call
// from both AddonInit and the worker thread. Never call from DllMain —
// dbghelp.dll is loaded lazily inside the crash filter.
void DebugLoggerInit();

// Timestamped append (open/write/close, ~micro-scale). Use for transitions
// and rare events only — never per tick.
void DebugLog(const char* fmt, ...);

}  // namespace senseforge

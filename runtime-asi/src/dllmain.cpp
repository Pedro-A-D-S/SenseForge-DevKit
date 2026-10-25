// SenseForge injectable runtime — entry point (SF-3).
//
// Subsystems arriving with their dedicated issues:
//   SF-4: hid_controller  (adaptive triggers L2/R2, Lightbar, Player LEDs)
//   SF-5: quad_audio_mixer (PortAudio 4ch 48kHz + XInput suppression)
//   SF-6: imgui_sony_overlay (DX11/DX12 Present hook, F5 / PS+Options)
//
// Loading contract (skill:cpp-asi-dualsense-runtime §1): this DLL is placed
// as senseforge.asi next to the game exe and loaded by the wtsapi32.dll
// proxy (ACOdyssey does not import version.dll — SF-11); DLL_PROCESS_ATTACH
// only spawns the dedicated worker thread (DisableThreadLibraryCalls) —
// the game's main thread is never blocked, and the worker waits for the
// game's own window + warmup delay before touching any API (anti-tamper).

#include <windows.h>

#include <filesystem>
#include <string>

#include "hot_reload.hpp"
#include "hid_controller.hpp"
#include "event_bridge.hpp"
#include "sf_debug_logger.hpp"
#ifndef SENSEFORGE_RESHADE_ADDON
#include "imgui_sony_overlay.hpp"
#endif
#include "ini_config.hpp"
#include "runtime_hub.hpp"
#include "ipc_pipe_client.hpp"
#include "memory_scanner.hpp"
#include "mixer_core.hpp"
#include "quad_audio_mixer.hpp"
#include "sense_json.hpp"
#include "state_engine.hpp"
#include "wav_loader.hpp"
#include "xinput_hook.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <vector>

namespace {

#ifdef SENSEFORGE_RESHADE_ADDON
// SF-14: in the ReShade add-on the controls live in ReShade's own overlay
// (reshade_panel.cpp, any graphics API), so the DX11 Sony overlay is not
// linked. This no-op stand-in keeps the worker loop identical for both
// builds; Install() reports "not installed" and every overlay path is
// skipped.
class SonyOverlay {
public:
    bool Install(const std::filesystem::path&) { return false; }
    void RequestToggle() {}
    void QueueControllerButtons(const senseforge::ControllerButtons&) {}
    void SetCategories(const std::vector<std::string>&) {}
    bool ConsumeTestEffect() { return false; }
};
#else
using senseforge::SonyOverlay;
#endif

std::filesystem::path ExeDir() {
    wchar_t exe[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    return std::filesystem::path(exe).parent_path();
}

// SF-11b: profile discovery per active game. Preferred: <exe-basename>.
// sense.json next to the executable; falls back to the single *.sense.json
// in the directory; legacy fallback keeps the original ACOdyssey file.
std::filesystem::path FindManifest(const std::filesystem::path& dir) {
    std::error_code ec;
    wchar_t exe[MAX_PATH]{};
    if (GetModuleFileNameW(nullptr, exe, MAX_PATH) != 0) {
        const auto stem =
            std::filesystem::path(exe).stem().wstring() + L".sense.json";
        const auto by_exe = dir / stem;
        if (std::filesystem::exists(by_exe, ec) && !ec) {
            return by_exe;
        }
    }
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (ec) {
            break;
        }
        // std::filesystem::path::extension() of "game.sense.json" is
        // ".json" — the old extension() comparison could never match the
        // double extension (live: expedition33.sense.json was ignored and
        // the worker fell back to the ACO manifest). Match the filename
        // suffix instead.
        const auto name = entry.path().filename().wstring();
        constexpr std::size_t kSuffixLen = 11;  // ".sense.json"
        if (name.size() >= kSuffixLen &&
            _wcsicmp(name.c_str() + (name.size() - kSuffixLen),
                     L".sense.json") == 0) {
            return entry.path();
        }
    }
    return dir / "acodyssey.sense.json";
}

// Dev tooling (opt-in, senseforge_user.ini [Dev] agent_autostart=1): the
// sf-agent (PowerShell executor used by cloud dev sessions) runs only while
// the game is being played. The worker wakes its scheduled task with
// schtasks /run — CREATE_NO_WINDOW (no console flash) and launched by the
// Task Scheduler, i.e. outside the Steam job object of the game. Re-woken
// every 5 min in case it died; /run on a running task is a no-op.
DWORD WINAPI DevAgentWakeThread(LPVOID /*param*/) {
    for (;;) {
        wchar_t cmd[] = L"schtasks.exe /run /tn SenseForgeAgent";
        STARTUPINFOW si{};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi{};
        if (CreateProcessW(nullptr, cmd, nullptr, nullptr, FALSE,
                           CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
            senseforge::DebugLog("dev agent: wake sent");
        } else {
            senseforge::DebugLog("dev agent: wake failed err=%lu",
                                 GetLastError());
        }
        Sleep(5 * 60 * 1000);
    }
}

DWORD WINAPI SenseForgeWorkerThread(LPVOID /*param*/) {
    const std::filesystem::path dir = ExeDir();

    // SF-11b triage: event log + crash minidump BEFORE anything else so a
    // death inside the gate still leaves a trace.
    senseforge::DebugLoggerInit();
    senseforge::DebugLog("worker start: dir=%s", dir.string().c_str());

    senseforge::IniDocument ini;
    ini.Load(dir / "senseforge_user.ini");

    // Warmup gate (SF-11): wait out the game's unpack/splash before
    // touching any graphics/audio/input API. FIXED DELAY from the ini —
    // no windowing APIs from init threads: EnumWindows polling during
    // early boot killed BOTH engines tested (ACOdyssey E8 bisection and
    // E33 live, 7s death with the add-on alone).
    const int warmup_ms = static_cast<int>(
        ini.GetInt("Runtime", "warmup_delay_ms", 30000));
    Sleep(static_cast<DWORD>(warmup_ms));
    // Bisection switches (SF-11 triage): disable a subsystem via
    // senseforge_user.ini — no rebuild needed.
    const bool enable_overlay =
        ini.GetBool("Runtime", "enable_overlay", true);
    const bool enable_audio =
        ini.GetBool("Runtime", "enable_audio", true);
    const bool enable_xinput_hook =
        ini.GetBool("Runtime", "enable_xinput_hook", true);
    const bool enable_hid =
        ini.GetBool("Runtime", "enable_hid", true);
    senseforge::DebugLog(
        "gate open: warmup_ms=%d overlay=%d audio=%d xinput_hook=%d hid=%d",
        warmup_ms, static_cast<int>(enable_overlay),
        static_cast<int>(enable_audio), static_cast<int>(enable_xinput_hook),
        static_cast<int>(enable_hid));

    if (ini.GetBool("Dev", "agent_autostart", false)) {
        HANDLE wake = CreateThread(nullptr, 0, DevAgentWakeThread, nullptr, 0,
                                   nullptr);
        if (wake != nullptr) {
            CloseHandle(wake);
        }
    }

    // In-game overlay (SF-6): Sony-style ImGui menu on a DX11 Present hook.
    // Installed FIRST on this thread: later subsystems (WASAPI/COM via
    // PortAudio, MinHook, HID) must not influence the swapchain creation —
    // a later install point failed live with E_INVALIDARG.
    SonyOverlay sony_overlay;
    const bool overlay_installed =
        enable_overlay
            ? sony_overlay.Install(dir / "senseforge_user.ini")
            : false;

    // HID/UDP controller (SF-4): intensity presets come straight from the
    // player's senseforge_user.ini (spec §3.2).
    senseforge::HidController hid_controller(
        senseforge::HidController::Settings{});
    hid_controller.SetIntensityPresets(
        static_cast<int>(ini.GetInt("AdaptiveTriggers", "left_trigger_intensity", 3)),
        static_cast<int>(ini.GetInt("AdaptiveTriggers", "right_trigger_intensity", 3)));

    // Quad-audio mixer (SF-5): 4-channel DualSense USB endpoint, gains from
    // senseforge_user.ini. Best-effort — no endpoint means no haptics audio,
    // never a game failure.
    senseforge::QuadAudioMixer audio_mixer;
    std::string audio_error;
    const bool audio_ok =
        enable_audio &&
        audio_mixer.Start(
            senseforge::QuadAudioMixer::Settings{
                "Wireless Controller",
                ini.GetFloat("ControllerSpeaker", "speaker_volume", 0.75),
                ini.GetFloat("Haptics", "custom_haptic_gain", 1.0),
                ini.GetFloat("Haptics", "soft_limiter_ceiling", 0.92),
                256},
            &audio_error);

    // XInputSetState hook (SF-5): the game's coarse rumble becomes Shadow
    // Play telemetry and is suppressed (HD Haptics owns vibration).
    std::atomic<bool> suppress_rumble{
        ini.GetBool("Haptics", "suppress_xinput_rumble", true)};
    std::atomic<bool> rumble_dirty{false};
    std::atomic<unsigned> rumble_left{0};
    std::atomic<unsigned> rumble_right{0};
    const auto capture_rumble = [&](unsigned /*user_index*/, unsigned left,
                                    unsigned right) {
        rumble_left.store(left);
        rumble_right.store(right);
        rumble_dirty.store(true);
    };
    const auto rumble_suppressed = [&]() {
        return suppress_rumble.load();
    };
    senseforge::XinputHook xinput_hook;
    if (enable_xinput_hook) {
        xinput_hook.Install(capture_rumble, rumble_suppressed);
    }

    // TODO(SF-11): profile discovery per active game (executable name match).
    const std::filesystem::path manifest_path = FindManifest(dir);
    senseforge::Manifest manifest;
    std::string manifest_error;
    bool have_manifest =
        senseforge::ParseManifest(manifest_path, manifest, &manifest_error);

    senseforge::IpcPipeClient ipc;  // \\.\pipe\senseforge_ipc → Studio (SF-7)
    ipc.WriteLine(
        "{\"type\":\"runtime_ready\",\"warmup_ms\":" +
        std::to_string(warmup_ms) + "}");
    senseforge::DebugLog(
        "subsystems: overlay_installed=%d audio_ok=%d xinput=%d manifest=%s "
        "ok=%d err=%s",
        static_cast<int>(overlay_installed), static_cast<int>(audio_ok),
        static_cast<int>(xinput_hook.installed()),
        manifest_path.string().c_str(), static_cast<int>(have_manifest),
        manifest_error.c_str());
    // Studio -> runtime requests (SF-7): trigger_test_effect arrives on
    // the same pipe the telemetry streams out of (reader thread).
    std::atomic<bool> ipc_test_effect{false};
    ipc.SetMessageHandler([&](const std::string& message) {
        if (message.find("\"trigger_test_effect\"") != std::string::npos) {
            ipc_test_effect.store(true);
        }
    });
    senseforge::MemoryScanner scanner;

    const auto push_categories = [&](const senseforge::Manifest& m) {
        std::vector<std::string> ids;
        for (const auto& state : m.states) {
            ids.push_back(state.category);
        }
        sony_overlay.SetCategories(ids);
    };
    if (have_manifest) {
        push_categories(manifest);
    }

    senseforge::HotReloadWatcher watcher;  // Poll() every ~5ms → <10ms reload
    watcher.Register(manifest_path,
                     [&](const std::filesystem::path& path) {
                         senseforge::Manifest fresh;
                         std::string error;
                         // Atomic swap: keep the old manifest on parse error
                         // so a half-written file never disables the runtime.
                         if (senseforge::ParseManifest(path, fresh, &error)) {
                             manifest = std::move(fresh);
                             have_manifest = true;
                             if (overlay_installed) {
                                 push_categories(manifest);
                             }
                             ipc.WriteLine(
                                 "{\"type\":\"manifest_reloaded\",\"path\":\"" +
                                 path.string() + "\"}");
                         } else {
                             ipc.WriteLine(
                                 "{\"type\":\"manifest_reload_rejected\","
                                 "\"error\":\"" +
                                 error + "\"}");
                         }
                     });
    watcher.Register(dir / "senseforge_user.ini",
                     [&](const std::filesystem::path& path) {
                         ini.Load(path);
                         // Live-refresh of hot settings (no restart needed).
                         suppress_rumble.store(ini.GetBool(
                             "Haptics", "suppress_xinput_rumble", true));
                         audio_mixer.core().SetMasterGains(
                             ini.GetFloat("ControllerSpeaker",
                                          "speaker_volume", 0.75),
                             ini.GetFloat("Haptics", "custom_haptic_gain",
                                          1.0));
                         audio_mixer.core().SetCeiling(
                             ini.GetFloat("Haptics", "soft_limiter_ceiling",
                                          0.92));
                         // SF-6: overlay edits to the trigger presets take
                         // effect on the next Apply.
                         hid_controller.SetIntensityPresets(
                             static_cast<int>(ini.GetInt(
                                 "AdaptiveTriggers",
                                 "left_trigger_intensity", 3)),
                             static_cast<int>(ini.GetInt(
                                 "AdaptiveTriggers",
                                 "right_trigger_intensity", 3)));
                         ipc.WriteLine(
                             "{\"type\":\"user_ini_reloaded\",\"path\":\"" +
                             path.string() + "\"}");
                     });

    // SF-12b: gameplay events from the UE4SS Lua bridge (Q9: parry flash
    // with lightbar + rumble motors). Two transports: non-blocking UDP
    // (when the Lua sandbox has sockets) and an NDJSON file tail (E33's
    // UE4SS build ships without LuaSocket — the mod appends lines next to
    // the game exe and we tail them with sub-tick latency).
    senseforge::EventBridge event_bridge(senseforge::EventBridge::Settings{
        static_cast<std::uint16_t>(ini.GetInt("Events", "udp_port", 6970))});
    event_bridge.Start();
    if (ini.GetBool("Events", "enable_file", true)) {
        event_bridge.StartFile(
            dir / ini.GetString("Events", "file", "senseforge_events.ndjson"));
    }

    // Worker loop: 5ms hot-reload tick · 50Hz state application against live
    // binding values (SEH fail-closed) · ~200ms telemetry heartbeat.
    std::string active_state = "boot";
    std::string active_loop_file;
    std::string last_logged_state;
    // SF-12b transient overlay (worker thread only). Resolved against the
    // LIVE manifest each application tick — never a pointer, so a
    // hot-reload of the manifest can't dangle it.
    std::string transient_id;
    std::int64_t transient_start_ms = 0;
    double transient_scale = 1.0;
    senseforge::DualsenseNav nav;

    // SF-14: status for the ReShade panel (published every ~200ms).
    senseforge::RuntimeHub& hub = senseforge::RuntimeHub::Instance();
    senseforge::RuntimeStatus status;
    status.worker_ready = true;
    status.ini_path = dir / "senseforge_user.ini";
    status.audio_ok = audio_ok;
    status.audio_device = audio_ok ? audio_mixer.device_name() : "";
    status.audio_error = audio_error;
    const auto fill_manifest_status = [&]() {
        status.manifest_path = manifest_path;
        status.manifest_ok = have_manifest;
        status.manifest_error = manifest_error;
        status.game_title = manifest.game_metadata.title;
        status.ui = manifest.ui;  // SF-17: the MOD describes its panel
        status.manifest_problems =
            have_manifest ? senseforge::ValidateManifest(manifest)
                          : std::vector<std::string>{};
        status.state_ids.clear();
        status.state_categories.clear();
        status.event_ids.clear();
        for (const auto& state : manifest.states) {
            status.state_ids.push_back(state.id);
            status.state_categories.push_back(state.category);
        }
        for (const auto& effect : manifest.event_effects) {
            if (effect.present) {
                status.event_ids.push_back(effect.id);
            }
        }
    };
#ifdef SENSEFORGE_MOD_VERSION
    status.mod_version = SENSEFORGE_MOD_VERSION;
#endif
    status.manifest_problems =
        have_manifest ? senseforge::ValidateManifest(manifest)
                      : std::vector<std::string>{};
    // SF-17 creator tools: last 50 gameplay events, as the worker saw them.
    const auto log_event = [&](const char* kind, const std::string& id,
                               double value, bool applied,
                               const char* note) {
        senseforge::EventLogEntry entry;
        entry.ms = GetTickCount64();
        entry.kind = kind;
        entry.id = id;
        entry.value = value;
        entry.applied = applied;
        entry.note = note;
        status.recent_events.push_back(std::move(entry));
        while (status.recent_events.size() > 50) {
            status.recent_events.pop_front();
        }
    };
    const auto has_effect = [&](const std::string& id) {
        return std::any_of(manifest.event_effects.begin(),
                           manifest.event_effects.end(),
                           [&](const senseforge::EventEffect& effect) {
                               return effect.present && effect.id == id;
                           });
    };
    senseforge::BindingValues last_event_values;
    const auto start_flash = [&](const std::string& id, double scale) {
        transient_id = id;
        transient_start_ms = static_cast<std::int64_t>(GetTickCount64());
        transient_scale = scale;
        status.last_flash_id = id;
        status.last_flash_ms = GetTickCount64();
        ++status.flash_count;
    };
    int last_live = 0;
    int last_dead = 0;

    for (unsigned long long tick = 0;; ++tick) {
        if ((tick % 4000) == 0) {  // ~20s liveness heartbeat
            senseforge::DebugLog("heartbeat tick=%llu state=%s",
                                 static_cast<unsigned long long>(tick),
                                 active_state.c_str());
        }
        watcher.Poll();
        if (enable_hid) {
            hid_controller.EnsureConnected();
        }

        // SF-12b: drain gameplay events (UE4SS Lua bridge) — bounded,
        // non-blocking; empty socket costs one recvfrom.
        event_bridge.Poll();
        event_bridge.PollFile();
        // SF-14: master switch from the panel's "Desligado" preset.
        const bool sf_enabled = ini.GetBool("SenseForge", "enabled", true);
        for (const auto& flash : event_bridge.pending_flashes()) {
            senseforge::DebugLog("event flash: id=%s scale=%.2f",
                                 flash.id.c_str(), flash.scale);
            ipc.WriteLine("{\"type\":\"game_flash\",\"id\":\"" + flash.id +
                          "\"}");
            // Disabled in the panel (master or per event): logged and
            // forwarded to Studio, never felt.
            if (!sf_enabled) {
                log_event("flash", flash.id, flash.scale, false,
                          "SenseForge desligado");
            } else if (!senseforge::EventEnabled(ini, flash.id)) {
                log_event("flash", flash.id, flash.scale, false,
                          "evento desligado no painel");
            } else if (!has_effect(flash.id)) {
                log_event("flash", flash.id, flash.scale, false,
                          "sem event_effect no manifesto");
            } else {
                start_flash(flash.id, flash.scale);
                log_event("flash", flash.id, flash.scale, true, "");
            }
        }
        event_bridge.ClearFlashes();
        if (const auto test_flash = hub.ConsumeTestFlash()) {
            senseforge::DebugLog("panel test flash: id=%s",
                                 test_flash->c_str());
            start_flash(*test_flash, 1.0);
            log_event("test", *test_flash, 1.0, has_effect(*test_flash),
                      has_effect(*test_flash) ? ""
                                              : "sem event_effect no manifesto");
        }
        // SF-17: persistent sf_state changes feed the creator log too.
        for (const auto& [key, value] : event_bridge.values()) {
            const auto previous = last_event_values.find(key);
            if (previous == last_event_values.end() ||
                previous->second != value) {
                log_event("state", key, value, sf_enabled, "");
                last_event_values[key] = value;
            }
        }
        // SF-17: "Recarregar manifesto" from the creator tools.
        if (hub.ConsumeReloadManifest()) {
            senseforge::Manifest fresh;
            std::string error;
            if (senseforge::ParseManifest(manifest_path, fresh, &error)) {
                manifest = std::move(fresh);
                have_manifest = true;
                manifest_error.clear();
                push_categories(manifest);
            } else {
                manifest_error = error;
            }
            status.manifest_problems =
                have_manifest ? senseforge::ValidateManifest(manifest)
                              : std::vector<std::string>{};
            senseforge::DebugLog("panel manifest reload: ok=%d problems=%zu",
                                 static_cast<int>(error.empty()),
                                 status.manifest_problems.size());
        }

        // Games load XInput lazily — retry the hook every ~2s until it is
        // in place (SF-5).
        if (enable_xinput_hook && !xinput_hook.installed() &&
            (tick % 400) == 0) {
            xinput_hook.Install(capture_rumble, rumble_suppressed);
        }

        // Captured rumble becomes an IPC telemetry event (Shadow Play input).
        if (rumble_dirty.exchange(false)) {
            ipc.WriteLine("{\"type\":\"xinput_rumble\",\"left\":" +
                          std::to_string(rumble_left.load()) +
                          ",\"right\":" +
                          std::to_string(rumble_right.load()) + "}");
        }

        // DualSense input → overlay (SF-6): PS+Options combo toggles the
        // menu; held buttons feed ImGui gamepad navigation (D-Pad/X/O).
        if (overlay_installed && (tick % 4) == 0) {
            senseforge::ControllerButtons buttons;
            if (hid_controller.TryReadInput(buttons)) {
                if (nav.Update(buttons)) {
                    sony_overlay.RequestToggle();
                }
                sony_overlay.QueueControllerButtons(nav.current());
            } else {
                senseforge::ControllerButtons none;
                sony_overlay.QueueControllerButtons(none);
            }
        }

        // "Testar Efeito Agora" (SF-6/SF-7): demo pulse — triggers vibrate
        // at 38Hz, lightbar flashes white, speaker beeps 440Hz and the
        // coils thump 60Hz. Fired by the overlay button or by Studio over
        // the pipe. Reverts on the next state-application tick.
        if ((overlay_installed && sony_overlay.ConsumeTestEffect()) ||
            ipc_test_effect.exchange(false) || hub.ConsumeTestEffect()) {
            senseforge::HidOutputCommand test;
            test.left_mode = senseforge::TriggerEffectMode::Vibration;
            test.right_mode = senseforge::TriggerEffectMode::Vibration;
            test.left = {0, 0, 0, 0, 7, 38};
            test.right = test.left;
            test.lightbar_rgb[0] = 255;
            test.lightbar_rgb[1] = 255;
            hid_controller.Apply(test);
            if (audio_ok) {
                constexpr std::size_t kSamples = 48000 / 10;  // 100ms @48k
                std::vector<float> speaker(kSamples);
                std::vector<float> coil(kSamples);
                constexpr double kPi = 3.14159265358979323846;
                for (std::size_t i = 0; i < kSamples; ++i) {
                    const double t = static_cast<double>(i) / 48000.0;
                    const double fade = std::min<double>(
                        {1.0, static_cast<double>(i) / 240.0,
                         static_cast<double>(kSamples - i) / 240.0});
                    speaker[i] = static_cast<float>(
                        0.35 * std::sin(2 * kPi * 440.0 * t) * fade);
                    coil[i] = static_cast<float>(
                        0.50 * std::sin(2 * kPi * 60.0 * t) * fade);
                }
                audio_mixer.core().AddOneShot(
                    senseforge::Bus::Speaker, speaker, speaker, 1.0);
                audio_mixer.core().AddOneShot(
                    senseforge::Bus::Haptics, coil, coil, 1.0);
            }
        }

        if (have_manifest && (tick % 4) == 0) {  // ~50Hz application cadence
            senseforge::BindingValues values;
            int live = 0;
            int dead = 0;
            for (const auto& [id, binding] : manifest.memory_bindings) {
                senseforge::BindingValue value;
                if (scanner.Read(binding, value)) {
                    values[id] = value.AsDouble();
                    ++live;
                } else {
                    ++dead;
                }
            }
            // SF-12b: persistent event values (UE4SS Lua "sf_state"
            // datagrams) — conditions reference the ids directly.
            for (const auto& [key, value] : event_bridge.values()) {
                values[key] = value;
            }

            // Highest-priority state whose condition holds (fail-closed
            // evaluator: unknown/unhealthy bindings never fire a state).
            // SF-6: states of a category disabled in the overlay are
            // skipped entirely.
            // SF-14: states switched off in the panel are skipped too.
            const senseforge::State* best = nullptr;
            for (const auto& state : manifest.states) {
                if (sf_enabled &&
                    (!best || state.priority > best->priority) &&
                    ini.GetBool("Categories", state.category, true) &&
                    senseforge::StateEnabled(ini, state.id) &&
                    senseforge::EvaluateCondition(state.condition, values)) {
                    best = &state;
                }
            }

            senseforge::HidOutputCommand command;
            bool have_command = false;
            if (best != nullptr) {
                command =
                    senseforge::ComposeCommand(manifest, *best, values, ini);
                // SF-6: belt-and-suspenders — UI states never actuate
                // triggers when the overlay asked for it.
                if (best->category == "ui" &&
                    ini.GetBool("AdaptiveTriggers", "disable_in_menus",
                                true)) {
                    command.left_mode = senseforge::TriggerEffectMode::Off;
                    command.right_mode = senseforge::TriggerEffectMode::Off;
                }
                have_command = true;
                active_state = best->id;
            } else {
                // No state matched: release the triggers (default command is
                // neutral). Without this the last state's effect lingered —
                // L2 stayed stiff after leaving free aim in exploration and
                // R2 stayed rigid after combat (debrief 25/10 PID 34420).
                have_command = true;
                active_state = sf_enabled ? "none" : "off";
            }
            if (!sf_enabled) {
                transient_id.clear();  // "Desligado": no flash is felt
            }

            // SF-12b transient overlay (Q9): while a gameplay flash
            // (parry, dodge...) is inside its fade window it REPLACES the
            // lightbar + rumble and optionally pulses both triggers,
            // regardless of whether a state matched. Resolved against the
            // live manifest — hot-reload safe.
            if (!transient_id.empty()) {
                const senseforge::EventEffect* effect = nullptr;
                for (const auto& candidate : manifest.event_effects) {
                    if (candidate.present && candidate.id == transient_id) {
                        effect = &candidate;
                        break;
                    }
                }
                const std::int64_t now_ms =
                    static_cast<std::int64_t>(GetTickCount64());
                const double elapsed_ms =
                    static_cast<double>(now_ms - transient_start_ms);
                const double fade =
                    effect == nullptr || effect->duration_ms <= 0
                        ? 0.0
                        : 1.0 - elapsed_ms / effect->duration_ms;
                if (effect != nullptr && fade > 0.0) {
                    // SF-14: panel overrides (enabled / intensity /
                    // lightbar) are applied inside ApplyEventEffect.
                    if (senseforge::ApplyEventEffect(
                            *effect, transient_scale * fade, ini, command)) {
                        have_command = true;
                    }
                } else {
                    transient_id.clear();  // window over — state rule resumes
                }
            }
            if (have_command) {
                hid_controller.Apply(command);
            }
            if (active_state != last_logged_state) {
                last_logged_state = active_state;
                senseforge::DebugLog("state -> %s (live=%d dead=%d)",
                                     active_state.c_str(), live, dead);
            }

            // Haptic loop (SF-5): the active state's loop file streams on
            // the coil pair (Bus::Haptics) with the gain driven live by the
            // gain_binding value (e.g. bow tension 0.0-1.0).
            const std::string loop_file =
                (best != nullptr && best->haptic_loop.present)
                    ? best->haptic_loop.file
                    : "";
            if (loop_file != active_loop_file) {
                active_loop_file = loop_file;
                if (loop_file.empty()) {
                    audio_mixer.core().ClearLoop(senseforge::Bus::Haptics);
                } else {
                    senseforge::WavData wav;
                    if (senseforge::LoadWav(dir / loop_file, wav, nullptr)) {
                        std::vector<float> left;
                        std::vector<float> right;
                        senseforge::SplitChannels(wav, left, right);
                        audio_mixer.core().SetLoop(senseforge::Bus::Haptics,
                                                   std::move(left),
                                                   std::move(right), 0.0);
                    }  // unreadable file: keep silent (fail-closed)
                }
            }
            if (best != nullptr) {
                double loop_gain = 0.0;
                const auto gain_it =
                    values.find(best->haptic_loop.gain_binding);
                if (gain_it != values.end()) {
                    loop_gain = std::clamp(gain_it->second, 0.0, 1.0);
                }
                audio_mixer.core().SetLoopGain(senseforge::Bus::Haptics,
                                                loop_gain);
                audio_mixer.core().SetMuted(best->haptics_mute);
            } else {
                // SF-14: "Desligado" also silences speaker + coils.
                audio_mixer.core().SetMuted(!sf_enabled);
            }
            last_live = live;
            last_dead = dead;
            status.binding_values = values;  // SF-17 creator tools
            status.active_priority = best != nullptr ? best->priority : 0;

            if ((tick % 40) == 0) {  // every ~200ms
                const char* transport = "none";
                switch (hid_controller.active_transport()) {
                    case senseforge::HidController::Transport::DirectHid:
                        transport = "direct_hid";
                        break;
                    case senseforge::HidController::Transport::UdpDsx:
                        transport = "udp_dsx";
                        break;
                    default:
                        break;
                }
                const std::string line =
                    "{\"type\":\"telemetry_frame\",\"state\":\"" +
                    active_state + "\",\"transport\":\"" + transport +
                    "\",\"audio\":\"" +
                    (audio_ok ? audio_mixer.device_name() : "none") +
                    "\",\"xinput_hook\":" +
                    (xinput_hook.installed() ? "true" : "false") +
                    ",\"overlay\":" +
                    (overlay_installed ? "true" : "false") +
                    ",\"bindings_live\":" + std::to_string(live) +
                    ",\"bindings_dead\":" + std::to_string(dead) + "}";
                ipc.WriteLine(line);
            }
        }

        // SF-14: status snapshot for the ReShade panel (~200ms), outside
        // the manifest block so a missing manifest still shows up there.
        if ((tick % 40) == 0) {
            fill_manifest_status();
            status.enabled = ini.GetBool("SenseForge", "enabled", true);
            status.active_state = active_state;
            status.bindings_live = last_live;
            status.bindings_dead = last_dead;
            switch (hid_controller.active_transport()) {
                case senseforge::HidController::Transport::DirectHid:
                    status.transport = "direct_hid";
                    status.hid_connected = true;
                    break;
                case senseforge::HidController::Transport::UdpDsx:
                    status.transport = "udp_dsx";
                    status.hid_connected = true;
                    break;
                default:
                    status.transport = "none";
                    status.hid_connected = false;
                    break;
            }
            hub.Publish(status);
        }

        Sleep(5);
    }
}

}  // namespace

// One worker per process + lifetime pin (SF-11b). ReShade loads and
// unloads add-ons repeatedly while the game's device comes up (4
// load/unload cycles measured live on E33 boot). Without the pin, a
// FreeLibrary unmaps a copy while its worker thread still sleeps in the
// warmup gate — waking into unmapped code produced the c0000005
// DEP-execute crash WER logged as senseforge.addon64_unloaded (offset
// 0x67fe, 2026-10-23). Without the mutex, every reload cycle spawns a
// duplicate worker (4 measured) contending for the same controller.
BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID /*reserved*/) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);

        // Pin this mapping for the whole process: host-driven unloads
        // (ReShade reload cycles) can never strand a worker in unmapped
        // code again.
        HMODULE pinned = nullptr;
        GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                GET_MODULE_HANDLE_EX_FLAG_PIN,
            reinterpret_cast<LPCWSTR>(module), &pinned);

        // Single-worker guard per process: the first mapping wins and
        // holds the named mutex until process death (the handle is
        // intentionally never closed).
        wchar_t mutex_name[64]{};
        _snwprintf_s(mutex_name, _TRUNCATE, L"Local\\SenseForgeWorker_%lu",
                     GetCurrentProcessId());
        HANDLE worker_guard = CreateMutexW(nullptr, TRUE, mutex_name);
        if (worker_guard != nullptr &&
            GetLastError() != ERROR_ALREADY_EXISTS) {
            HANDLE thread =
                CreateThread(nullptr, 0, SenseForgeWorkerThread, nullptr, 0,
                             nullptr);
            if (thread != nullptr) {
                CloseHandle(thread);
            } else {
                // Give a later reload cycle the chance to spawn instead.
                ReleaseMutex(worker_guard);
                CloseHandle(worker_guard);
            }
        }
    }
    return TRUE;
}

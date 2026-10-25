cmake_minimum_required(VERSION 3.21)

project(SenseForgeRuntime VERSION 0.1.0 LANGUAGES C CXX ASM_MASM)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "SenseForge runtime is x64-only (the game target is 64-bit).")
endif()

if(MSVC)
    # C++-only options — MASM (ml64) rejects /permissive- and /MP.
    add_compile_options(
        $<$<COMPILE_LANGUAGE:CXX>:/W4>
        $<$<COMPILE_LANGUAGE:CXX>:/permissive->
        $<$<COMPILE_LANGUAGE:CXX>:/MP>
    )
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

# ---------------------------------------------------------------------------
# Third-party probe (sources fetched by tools/fetch_third_party.sh, pinned)
# Full integration of MinHook/PortAudio/ImGui lands in SF-3..SF-6; the probe
# keeps configure green on a fresh clone before the fetch script has run.
# ---------------------------------------------------------------------------
set(SENSEFORGE_THIRD_PARTY_DIR "$ENV{LOCALAPPDATA}/senseforge/third_party" CACHE PATH "Pinned open-source dependencies (see tools/fetch_third_party.sh)")

set(SENSEFORGE_HAS_MINHOOK    OFF)
set(SENSEFORGE_HAS_PORTAUDIO  OFF)
set(SENSEFORGE_HAS_IMGUI      OFF)
set(SENSEFORGE_HAS_NLOHMANN   OFF)
set(SENSEFORGE_HAS_DUALSENSITIVE OFF)

if(EXISTS "${SENSEFORGE_THIRD_PARTY_DIR}/minhook/include/MinHook.h")
    set(SENSEFORGE_HAS_MINHOOK ON)
endif()
if(EXISTS "${SENSEFORGE_THIRD_PARTY_DIR}/portaudio/include/portaudio.h")
    set(SENSEFORGE_HAS_PORTAUDIO ON)
endif()
if(EXISTS "${SENSEFORGE_THIRD_PARTY_DIR}/imgui/imgui.h")
    set(SENSEFORGE_HAS_IMGUI ON)
endif()
if(EXISTS "${SENSEFORGE_THIRD_PARTY_DIR}/nlohmann_json/single_include/nlohmann/json.hpp")
    set(SENSEFORGE_HAS_NLOHMANN ON)
endif()
if(EXISTS "${SENSEFORGE_THIRD_PARTY_DIR}/dualsensitive/src/core/DS5_Output.cpp")
    set(SENSEFORGE_HAS_DUALSENSITIVE ON)
endif()

message(STATUS "SenseForge third-party probe: MinHook=${SENSEFORGE_HAS_MINHOOK} PortAudio=${SENSEFORGE_HAS_PORTAUDIO} ImGui=${SENSEFORGE_HAS_IMGUI} nlohmann=${SENSEFORGE_HAS_NLOHMANN} dualsensitive=${SENSEFORGE_HAS_DUALSENSITIVE}")

if(NOT SENSEFORGE_HAS_NLOHMANN)
    message(FATAL_ERROR "nlohmann/json (pinned v3.12.0) is required by sense_json.cpp — run tools/fetch_third_party.sh first.")
endif()
if(NOT SENSEFORGE_HAS_DUALSENSITIVE)
    message(FATAL_ERROR "dualsensitive (vendored) is required by hid_controller.cpp — run tools/fetch_third_party.sh first.")
endif()
if(NOT SENSEFORGE_HAS_PORTAUDIO)
    message(FATAL_ERROR "PortAudio (vendored) is required by quad_audio_mixer.cpp — run tools/fetch_third_party.sh first.")
endif()
if(NOT SENSEFORGE_HAS_MINHOOK)
    message(FATAL_ERROR "MinHook (vendored) is required by xinput_hook.cpp — run tools/fetch_third_party.sh first.")
endif()

set(SENSEFORGE_JSON_INCLUDE "${SENSEFORGE_THIRD_PARTY_DIR}/nlohmann_json/single_include")

# ---------------------------------------------------------------------------
# dualsensitive core (MIT, tpetsas / Ohjurot DualSense-Windows) — DS5W
# direct HID transport, trigger profiles, BT CRC32 (SF-4).
# ---------------------------------------------------------------------------
set(SENSEFORGE_DUALSENSITIVE_DIR "${SENSEFORGE_THIRD_PARTY_DIR}/dualsensitive")
add_library(dualsensitive_core STATIC
    "${SENSEFORGE_DUALSENSITIVE_DIR}/src/dualsensitive.cpp"
    "${SENSEFORGE_DUALSENSITIVE_DIR}/src/core/DS5_Input.cpp"
    "${SENSEFORGE_DUALSENSITIVE_DIR}/src/core/DS5_Output.cpp"
    "${SENSEFORGE_DUALSENSITIVE_DIR}/src/core/DS_CRC32.cpp"
    "${SENSEFORGE_DUALSENSITIVE_DIR}/src/core/IO.cpp"
    "${SENSEFORGE_DUALSENSITIVE_DIR}/src/core/Helpers.cpp"
    "${SENSEFORGE_DUALSENSITIVE_DIR}/src/core/logger.cpp"
    "${SENSEFORGE_DUALSENSITIVE_DIR}/src/core/udp/udp.cpp"
)
target_include_directories(dualsensitive_core PUBLIC
    "${SENSEFORGE_DUALSENSITIVE_DIR}/include"
    "${SENSEFORGE_DUALSENSITIVE_DIR}/src/core"
    "${SENSEFORGE_DUALSENSITIVE_DIR}/src/core/udp"
)
# PUBLIC: consumers of the static lib include the DS5W headers too and must
# see the same linkage model (otherwise DSW_Api.h defaults to dllimport).
target_compile_definitions(dualsensitive_core PUBLIC DS5W_BUILD_LIB)
# Vendored dependency: ISO-8859 comments (C4819), local NOMINMAX redefinition
# (C4005) and its own quality bar. The /FI shim restores the windows.h
# min/max macros that our global NOMINMAX strips (DS5_Output.cpp uses max).
if(MSVC)
    target_compile_options(dualsensitive_core PRIVATE
        /W3 /wd4819 /wd4005
        "/FI${CMAKE_CURRENT_SOURCE_DIR}/cmake/dualsensitive_compat.h"
    )
endif()
target_link_libraries(dualsensitive_core PUBLIC Hid Setupapi Ws2_32)

# ---------------------------------------------------------------------------
# PortAudio v19.7.0 (MIT, WASAPI) and MinHook (BSD) — vendored (SF-5).
# ---------------------------------------------------------------------------
set(PA_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(PA_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(PA_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
# WDM/KS host api does not compile against Windows SDK 10.0.26100
# (DRV_RESERVED/MMSYSERR_* gone from the modern headers); DirectSound
# (pa_win_ds_dynlink.c) breaks the same way (>100 errors). The DualSense
# runs on WASAPI, which stays ON. See third_party_patches/portaudio/.
set(PA_USE_WDMKS OFF CACHE BOOL "" FORCE)
set(PA_USE_WDMKS_DEVICE_INFO OFF CACHE BOOL "" FORCE)
set(PA_USE_DS OFF CACHE BOOL "" FORCE)
add_subdirectory("${SENSEFORGE_THIRD_PARTY_DIR}/portaudio" portaudio-build)
add_subdirectory("${SENSEFORGE_THIRD_PARTY_DIR}/minhook" minhook-build)

# ---------------------------------------------------------------------------
# Dear ImGui (vendored, MIT) — core + DX11/Win32 backends (SF-6 overlay).
# ---------------------------------------------------------------------------
add_library(imgui STATIC
    "${SENSEFORGE_THIRD_PARTY_DIR}/imgui/imgui.cpp"
    "${SENSEFORGE_THIRD_PARTY_DIR}/imgui/imgui_draw.cpp"
    "${SENSEFORGE_THIRD_PARTY_DIR}/imgui/imgui_tables.cpp"
    "${SENSEFORGE_THIRD_PARTY_DIR}/imgui/imgui_widgets.cpp"
    "${SENSEFORGE_THIRD_PARTY_DIR}/imgui/backends/imgui_impl_dx11.cpp"
    "${SENSEFORGE_THIRD_PARTY_DIR}/imgui/backends/imgui_impl_win32.cpp")
target_include_directories(imgui PUBLIC
    "${SENSEFORGE_THIRD_PARTY_DIR}/imgui"
    "${SENSEFORGE_THIRD_PARTY_DIR}/imgui/backends")
target_compile_options(imgui PRIVATE /W0)  # vendored: not our warnings
target_compile_definitions(imgui PRIVATE
    IMGUI_DISABLE_OBSOLETE_FUNCTIONS IMGUI_DISABLE_DEMO_WINDOWS)

# ---------------------------------------------------------------------------
# senseforge.asi — injectable runtime DLL (renamed .dll -> .asi).
# Loaded by the version.dll proxy placed next to the game executable.
# ---------------------------------------------------------------------------
add_library(senseforge_asi SHARED
    src/dllmain.cpp
    src/sf_debug_logger.cpp
    src/event_bridge.cpp
    src/memory_scanner.cpp
    src/hot_reload.cpp
    src/ini_config.cpp
    src/sense_json.cpp
    src/ipc_pipe_client.cpp
    src/state_engine.cpp
    src/hid_report_builder.cpp
    src/udp_dsx_client.cpp
    src/hid_controller.cpp
    src/wav_loader.cpp
    src/mixer_core.cpp
    src/quad_audio_mixer.cpp
    src/xinput_hook.cpp
    src/dualsense_input.cpp
    src/overlay_settings.cpp
    src/imgui_sony_overlay.cpp
    src/runtime_hub.cpp
)
target_include_directories(senseforge_asi PRIVATE src ${SENSEFORGE_JSON_INCLUDE}
    "${SENSEFORGE_THIRD_PARTY_DIR}/portaudio/include"
    "${SENSEFORGE_THIRD_PARTY_DIR}/minhook/include")
target_link_libraries(senseforge_asi PRIVATE dualsensitive_core
    portaudio_static minhook imgui d3d11 dxgi Shell32)
set_target_properties(senseforge_asi PROPERTIES
    OUTPUT_NAME "senseforge"
    SUFFIX ".asi"
)

# ---------------------------------------------------------------------------
# senseforge_addon — the same runtime packaged as a ReShade add-on (SF-11b):
# senseforge.addon64 in the game directory, loaded by the player's own
# ReShade (same route as community add-ons like RenoDX). LoadLibrary runs
# our existing DllMain, which spawns the same worker thread as the ASI
# deployment; AddonInit/AddonUninit are the optional ReShade lifecycle
# exports (signatures per ReShade 6.8 addon_manager.cpp).
# (The UE4SS C++ DLL-mod route was tried first: its lifecycle API requires
# building inside the UE4SS tree — UE4SS stays in the plan as the Lua event
# source for SF-12b.)
# ---------------------------------------------------------------------------
# SF-14: one add-on per game, named like RenoDX's (renodx-<game>.addon64):
# senseforge.<slug>.addon64. The runtime itself stays generic (manifest by
# suffix); the slug only names the file, the NAME export and VERSIONINFO.
set(SENSEFORGE_GAME_SLUG "expedition-33" CACHE STRING
    "Game slug of the ReShade add-on file: senseforge.<slug>.addon64")
set(SENSEFORGE_GAME_NAME "Expedition 33" CACHE STRING
    "Game name shown in ReShade's Add-ons list")
# SF-15: the MOD version (mods/<slug>/mod.json, passed by CI) names the
# add-on's VERSIONINFO and the panel header; defaults to the runtime's.
set(SENSEFORGE_MOD_VERSION "${PROJECT_VERSION}" CACHE STRING
    "MOD version (major.minor.patch) embedded in the add-on")
string(REGEX MATCH "^([0-9]+)\.([0-9]+)\.([0-9]+)" _sf_mod_ver
       "${SENSEFORGE_MOD_VERSION}")
if(NOT _sf_mod_ver)
    message(FATAL_ERROR "SENSEFORGE_MOD_VERSION must be major.minor.patch")
endif()
set(SENSEFORGE_MOD_VERSION_MAJOR ${CMAKE_MATCH_1})
set(SENSEFORGE_MOD_VERSION_MINOR ${CMAKE_MATCH_2})
set(SENSEFORGE_MOD_VERSION_PATCH ${CMAKE_MATCH_3})
configure_file(src/senseforge_addon.rc.in
    "${CMAKE_CURRENT_BINARY_DIR}/senseforge_addon.rc" @ONLY)

# SF-14: the add-on draws its UI through ReShade's own ImGui (the
# "SenseForge" overlay tab, any graphics API), so it links NO ImGui and
# not the DX11 Sony overlay: reshade_overlay.hpp defines inline ImGui::
# functions that would collide with the vendored library. Its imgui.h is
# the header-only v1.92.5 copy ReShade 6.8 requires (imgui_reshade).
add_library(senseforge_addon SHARED
    src/dllmain.cpp
    src/reshade_addon_main.cpp
    src/reshade_panel.cpp
    src/runtime_hub.cpp
    src/sf_debug_logger.cpp
    src/event_bridge.cpp
    src/memory_scanner.cpp
    src/hot_reload.cpp
    src/ini_config.cpp
    src/sense_json.cpp
    src/ipc_pipe_client.cpp
    src/state_engine.cpp
    src/hid_report_builder.cpp
    src/udp_dsx_client.cpp
    src/hid_controller.cpp
    src/wav_loader.cpp
    src/mixer_core.cpp
    src/quad_audio_mixer.cpp
    src/xinput_hook.cpp
    src/dualsense_input.cpp
    src/overlay_settings.cpp
    "${CMAKE_CURRENT_BINARY_DIR}/senseforge_addon.rc"
)
target_include_directories(senseforge_addon PRIVATE src ${SENSEFORGE_JSON_INCLUDE}
    src/reshade_api
    "${SENSEFORGE_THIRD_PARTY_DIR}/imgui_reshade"
    "${SENSEFORGE_THIRD_PARTY_DIR}/portaudio/include"
    "${SENSEFORGE_THIRD_PARTY_DIR}/minhook/include")
target_compile_definitions(senseforge_addon PRIVATE
    SENSEFORGE_RESHADE_ADDON
    SENSEFORGE_GAME_NAME="${SENSEFORGE_GAME_NAME}"
    SENSEFORGE_MOD_VERSION="${SENSEFORGE_MOD_VERSION}")
# Panel strings are PT-BR UTF-8 literals. /utf-8 only on the panel: the
# vendored dualsensitive headers are not UTF-8 (C4828 everywhere else).
set_source_files_properties(src/reshade_panel.cpp PROPERTIES
    COMPILE_OPTIONS "/utf-8")
target_link_libraries(senseforge_addon PRIVATE dualsensitive_core
    portaudio_static minhook Shell32)
# SF-11b triage: linker map so crash RVAs from the minidump resolve to
# symbols without a debugger (see tools/analyze_dump.py) — now
# senseforge.<slug>.map.
set_target_properties(senseforge_addon PROPERTIES
    OUTPUT_NAME "senseforge.${SENSEFORGE_GAME_SLUG}"
    SUFFIX ".addon64"
    LINK_FLAGS "/MAP"
)

# ---------------------------------------------------------------------------
# version.dll proxy loader (SF-1 skeleton — full export forwarding + ASI
# loading in the style of Ultimate ASI Loader lands in SF-3).
# ---------------------------------------------------------------------------
add_library(version_proxy SHARED
    src/version_proxy.cpp
    src/version_thunks.asm
    src/version_proxy.def
)
set_target_properties(version_proxy PROPERTIES
    OUTPUT_NAME "version"
)

# ---------------------------------------------------------------------------
# Sanity tests (parser/HID packet tests arrive with SF-3/SF-4).
# ---------------------------------------------------------------------------
enable_testing()

function(sf_add_test name)
    add_executable(${name} ${ARGN})
    target_include_directories(${name} PRIVATE src ${SENSEFORGE_JSON_INCLUDE})
    add_test(NAME ${name} COMMAND ${name})
endfunction()

sf_add_test(sf_sanity_tests    tests/test_sanity.cpp)
sf_add_test(sf_ini_tests       tests/test_ini.cpp src/ini_config.cpp)
sf_add_test(sf_json_tests      tests/test_sense_json.cpp src/sense_json.cpp src/memory_scanner.cpp)
sf_add_test(sf_scanner_tests   tests/test_memory_scanner.cpp src/memory_scanner.cpp)
sf_add_test(sf_hotreload_tests tests/test_hot_reload.cpp src/hot_reload.cpp)
sf_add_test(sf_state_tests     tests/test_state_engine.cpp src/state_engine.cpp)
sf_add_test(sf_hid_tests       tests/test_hid_report.cpp src/hid_report_builder.cpp)
sf_add_test(sf_udp_tests       tests/test_udp_dsx.cpp src/udp_dsx_client.cpp)
sf_add_test(sf_hc_tests        tests/test_hid_controller.cpp src/hid_controller.cpp src/hid_report_builder.cpp src/state_engine.cpp src/udp_dsx_client.cpp src/sense_json.cpp src/ini_config.cpp src/sf_debug_logger.cpp)
# SF-14: [States] / [EventEffects] overrides of the ReShade panel.
sf_add_test(sf_override_tests  tests/test_effect_overrides.cpp src/hid_controller.cpp src/hid_report_builder.cpp src/state_engine.cpp src/udp_dsx_client.cpp src/sense_json.cpp src/ini_config.cpp src/sf_debug_logger.cpp)
target_link_libraries(sf_override_tests PRIVATE dualsensitive_core Ws2_32 Shell32)
sf_add_test(sf_event_tests     tests/test_event_bridge.cpp src/event_bridge.cpp src/sf_debug_logger.cpp)
sf_add_test(sf_mixer_tests     tests/test_mixer_core.cpp src/mixer_core.cpp)
sf_add_test(sf_wav_tests       tests/test_wav_loader.cpp src/wav_loader.cpp)
sf_add_test(sf_xinput_tests    tests/test_xinput_hook.cpp src/xinput_hook.cpp)
sf_add_test(sf_quad_tests      tests/test_quad_audio.cpp src/quad_audio_mixer.cpp src/mixer_core.cpp)

# Tests that touch the vendored core (CRC equivalence) or raw sockets.
target_link_libraries(sf_hid_tests PRIVATE dualsensitive_core)
target_link_libraries(sf_udp_tests PRIVATE Ws2_32)
target_link_libraries(sf_hc_tests PRIVATE dualsensitive_core Ws2_32 Shell32)
target_link_libraries(sf_event_tests PRIVATE Ws2_32 Shell32)

# SF-5: hook and audio transport tests.
target_include_directories(sf_xinput_tests PRIVATE
    "${SENSEFORGE_THIRD_PARTY_DIR}/minhook/include")
target_link_libraries(sf_xinput_tests PRIVATE minhook)
target_include_directories(sf_quad_tests PRIVATE
    "${SENSEFORGE_THIRD_PARTY_DIR}/portaudio/include")
target_link_libraries(sf_quad_tests PRIVATE portaudio_static)

# SF-6: overlay — nav state machine, ini-backed settings model, and the
# DX11 sandbox (real GPU: swapchain + Present hook + menu render; skips
# with exit 0 where no adapter/D3D11 is available).
sf_add_test(sf_nav_tests       tests/test_dualsense_nav.cpp src/dualsense_input.cpp)
sf_add_test(sf_settings_tests  tests/test_overlay_settings.cpp src/overlay_settings.cpp src/ini_config.cpp)
sf_add_test(sf_overlay_dx11_tests tests/test_dx11_overlay.cpp
    src/imgui_sony_overlay.cpp src/dualsense_input.cpp
    src/overlay_settings.cpp src/ini_config.cpp)
target_link_libraries(sf_overlay_dx11_tests PRIVATE imgui d3d11 dxgi minhook)

# ---------------------------------------------------------------------------
# sf_injection_host — NOT a ctest: it is the live SF-3 validation exe. It is
# copied into a scratch directory together with version.dll + senseforge.asi
# + a fixture manifest and run against a Named Pipe server (see TIM-7
# evidence). Links the real version import lib: the loader must resolve the
# static version.dll import against our proxy.
# ---------------------------------------------------------------------------
add_executable(sf_injection_host tests/injection_host.cpp)
target_link_libraries(sf_injection_host version)

# ---------------------------------------------------------------------------
# sf_manifest_check — manifest validation tool (SF-10): runs the real
# ParseManifest + ValidateManifest over any .sense.json and reports
# bindings/states/triggers plus referential problems. Used to validate the
# generated acodyssey.sense.json against the runtime parser, and during
# SF-11 live shadow play to sanity-check hot-reloaded revisions.
# ---------------------------------------------------------------------------
add_executable(sf_manifest_check tests/manifest_check.cpp src/sense_json.cpp)
target_include_directories(sf_manifest_check PRIVATE src ${SENSEFORGE_JSON_INCLUDE})

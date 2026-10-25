// reshade_imgui.hpp — SF-14: the ONLY way add-on sources include ReShade.
//
// ReShade's header-only API compiles differently with and without Dear
// ImGui: with IMGUI_VERSION_NUM defined, reshade::register_addon also
// fetches ReShade's ImGui function table, and reshade_overlay.hpp turns
// every ImGui:: call into a call through that table (ReShade draws the UI
// for DX11, DX12 and Vulkan alike). register_addon is an inline function,
// so every translation unit must see the same definition: include this
// header, never <reshade.hpp> directly.
//
// The imgui.h on the include path is the header-only v1.92.5 copy
// (third_party/imgui_reshade, IMGUI_VERSION_NUM 19250) that ReShade 6.8's
// reshade_overlay.hpp requires — NOT the vendored 1.93 library the .asi
// build links (the add-on links no ImGui at all).

#pragma once

#include <windows.h>

#define ImTextureID ImU64
#include <imgui.h>

#include <reshade.hpp>

// reshade_panel.hpp — SF-14: the "SenseForge" tab of ReShade's overlay.

#pragma once

namespace reshade::api {
struct effect_runtime;
}

namespace senseforge {

// reshade::register_overlay callback: draws the SenseForge tab (presets,
// live status, global settings, per-effect overrides). Runs on ReShade's
// render thread; talks to the worker only through RuntimeHub and
// senseforge_user.ini.
void DrawReshadePanel(reshade::api::effect_runtime* runtime);

}  // namespace senseforge

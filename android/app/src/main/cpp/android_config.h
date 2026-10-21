#pragma once

namespace AndroidConfig {
constexpr float ZOOM_MIN = 0.05f;
constexpr float ZOOM_MAX = 20.0f;
constexpr float EXPOSURE_MIN = 0.01f;
constexpr float EXPOSURE_MAX = 10.0f;
constexpr float GAMMA_MIN = 1.0f;
constexpr float GAMMA_MAX = 4.0f;
constexpr float DEFAULT_EXPOSURE = 1.0f;
constexpr float DEFAULT_GAMMA = 2.2f;
constexpr float DEFAULT_ZOOM = 1.0f;

// HDR data convention: linear BT.709, 1.0 = 203 nits.
constexpr float HDR_WHITE_NITS = 203.0f;

// ImGui layout (pixels before density scaling).
constexpr float FONT_SIZE_PX = 13.0f;
constexpr float PANEL_MAX_WIDTH = 280.0f;
constexpr float PANEL_MAX_HEIGHT = 420.0f;
constexpr float PANEL_LANDSCAPE_RATIO = 0.32f;
constexpr float PANEL_PORTRAIT_RATIO = 0.46f;

// Touch handling.
constexpr float TAP_SLOP_PX = 12.0f;
constexpr int TAP_MAX_DURATION_MS = 250;
constexpr int DOUBLE_TAP_MS = 300;
}

#pragma once
// =============================================================================
// SmoothZoom — Common Types
// Shared data structures, constants, and type aliases.
// =============================================================================

#include <cstdint>
#include <atomic>

namespace SmoothZoom
{

// Screen coordinate pair
struct ScreenPoint
{
    int32_t x = 0;
    int32_t y = 0;
};

// Rectangle (screen coordinates)
struct ScreenRect
{
    int32_t left = 0;
    int32_t top = 0;
    int32_t right = 0;
    int32_t bottom = 0;

    int32_t width() const { return right - left; }
    int32_t height() const { return bottom - top; }
    ScreenPoint center() const { return {(left + right) / 2, (top + bottom) / 2}; }
};

// Keyboard command IDs (posted via lock-free queue from hook callbacks to render thread)
enum class ZoomCommand : uint8_t
{
    None = 0,
    ZoomIn,         // Win+Plus
    ZoomOut,        // Win+Minus
    ResetZoom,      // Win+Esc
    ToggleEngage,   // Ctrl+Alt press
    ToggleRelease,  // Ctrl+Alt release
    OpenSettings,   // Win+; (Phase 5)
    ToggleInvert,   // Win+I (Phase 6)
};

// Viewport tracking source priority (Doc 3 §3.6 — ViewportTracker)
enum class TrackingSource : uint8_t
{
    Pointer,    // Default: follow mouse pointer
    Focus,      // UIA focus-changed event
    Caret,      // UIA text caret / GTTI poll
};

} // namespace SmoothZoom

// =============================================================================
// SmoothZoom — ViewportTracker
// Offset computation, proportional pointer mapping. Doc 3 §3.4
//
// Phase 0: hardcoded center offset (no proportional tracking yet).
// Phase 1: Full proportional mapping (AC-2.4.01), edge clamping, deadzone.
// Phase 3: Multi-source priority arbitration (focus, caret, pointer).
// =============================================================================

#include "smoothzoom/logic/ViewportTracker.h"
#include <algorithm>

namespace SmoothZoom
{

// Core proportional mapping formula from Doc 3 §3.4:
//   xOffset = pointerX * (1.0 - 1.0 / zoom)
//   yOffset = pointerY * (1.0 - 1.0 / zoom)
//
// This has the elegant property that the desktop coordinate under the pointer
// is always (pointerX, pointerY), regardless of zoom level. This means
// zoom-center stability comes for free: zooming in/out while the pointer is
// stationary keeps the same desktop content under the pointer.
//
// Phase 0 note: We implement the formula but Phase 0 calls it with
// hardcoded center coordinates. Phase 1 activates full tracking.

ViewportTracker::Offset ViewportTracker::computePointerOffset(
    int32_t pointerX, int32_t pointerY,
    float zoom, int32_t screenW, int32_t screenH)
{
    if (zoom <= 1.0f)
        return {0.0f, 0.0f};

    float invZoom = 1.0f / zoom;

    float xOff = static_cast<float>(pointerX) * (1.0f - invZoom);
    float yOff = static_cast<float>(pointerY) * (1.0f - invZoom);

    // Clamp to valid offset range (viewport cannot pan past desktop edges)
    float maxOffX = static_cast<float>(screenW) * (1.0f - invZoom);
    float maxOffY = static_cast<float>(screenH) * (1.0f - invZoom);

    xOff = std::clamp(xOff, 0.0f, maxOffX);
    yOff = std::clamp(yOff, 0.0f, maxOffY);

    return {xOff, yOff};
}

ViewportTracker::Offset ViewportTracker::computeElementOffset(
    const ScreenRect& elementRect,
    float zoom, int32_t screenW, int32_t screenH)
{
    if (zoom <= 1.0f)
        return {0.0f, 0.0f};

    // Center the element in the viewport
    float viewportW = static_cast<float>(screenW) / zoom;
    float viewportH = static_cast<float>(screenH) / zoom;

    ScreenPoint center = elementRect.center();

    float xOff = static_cast<float>(center.x) - viewportW / 2.0f;
    float yOff = static_cast<float>(center.y) - viewportH / 2.0f;

    // Clamp to desktop bounds
    float maxOffX = static_cast<float>(screenW) - viewportW;
    float maxOffY = static_cast<float>(screenH) - viewportH;

    xOff = std::clamp(xOff, 0.0f, maxOffX);
    yOff = std::clamp(yOff, 0.0f, maxOffY);

    return {xOff, yOff};
}

TrackingSource ViewportTracker::determineActiveSource(
    int64_t /*lastPointerMoveTime*/,
    int64_t /*lastFocusChangeTime*/,
    int64_t /*lastKeyboardInputTime*/) const
{
    // Phase 0–2: Pointer only. Phase 3 adds focus/caret priority arbitration.
    return TrackingSource::Pointer;
}

} // namespace SmoothZoom

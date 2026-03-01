// =============================================================================
// SmoothZoom — ViewportTracker
// Offset computation, proportional pointer mapping. Doc 3 §3.6
//
// Phase 1: Full proportional mapping (AC-2.4.01), edge clamping.
//          Deadzone filtering lives in RenderLoop (AC-2.4.09–AC-2.4.11).
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

ViewportTracker::Offset ViewportTracker::computePointerOffset(
    int32_t pointerX, int32_t pointerY,
    float zoom, int32_t screenW, int32_t screenH,
    int32_t originX, int32_t originY)
{
    if (zoom <= 1.0f)
        return {0.0f, 0.0f};

    float invZoom = 1.0f / zoom;

    float xOff = static_cast<float>(pointerX) * (1.0f - invZoom);
    float yOff = static_cast<float>(pointerY) * (1.0f - invZoom);

    // Clamp to valid offset range (viewport cannot pan past virtual desktop edges)
    float minOffX = static_cast<float>(originX);
    float minOffY = static_cast<float>(originY);
    float maxOffX = static_cast<float>(originX) + static_cast<float>(screenW) * (1.0f - invZoom);
    float maxOffY = static_cast<float>(originY) + static_cast<float>(screenH) * (1.0f - invZoom);

    xOff = std::clamp(xOff, minOffX, maxOffX);
    yOff = std::clamp(yOff, minOffY, maxOffY);

    return {xOff, yOff};
}

ViewportTracker::Offset ViewportTracker::computeElementOffset(
    const ScreenRect& elementRect,
    float zoom, int32_t screenW, int32_t screenH,
    int32_t originX, int32_t originY)
{
    if (zoom <= 1.0f)
        return {0.0f, 0.0f};

    // Center the element in the viewport
    float viewportW = static_cast<float>(screenW) / zoom;
    float viewportH = static_cast<float>(screenH) / zoom;

    ScreenPoint center = elementRect.center();

    float xOff = static_cast<float>(center.x) - viewportW / 2.0f;
    float yOff = static_cast<float>(center.y) - viewportH / 2.0f;

    // Clamp to virtual desktop bounds
    float minOffX = static_cast<float>(originX);
    float minOffY = static_cast<float>(originY);
    float maxOffX = static_cast<float>(originX) + static_cast<float>(screenW) - viewportW;
    float maxOffY = static_cast<float>(originY) + static_cast<float>(screenH) - viewportH;

    xOff = std::clamp(xOff, minOffX, maxOffX);
    yOff = std::clamp(yOff, minOffY, maxOffY);

    return {xOff, yOff};
}

// Caret offset with lookahead margin (AC-2.6.06):
// Shifts the target ~15% of viewport width ahead of the caret so the user
// can see upcoming text. Assumes LTR typing direction (positive X shift).
ViewportTracker::Offset ViewportTracker::computeCaretOffset(
    const ScreenRect& caretRect,
    float zoom, int32_t screenW, int32_t screenH,
    int32_t originX, int32_t originY)
{
    if (zoom <= 1.0f)
        return {0.0f, 0.0f};

    float viewportW = static_cast<float>(screenW) / zoom;
    float viewportH = static_cast<float>(screenH) / zoom;

    ScreenPoint center = caretRect.center();

    // Apply lookahead: shift target ahead of caret in typing direction
    float lookahead = viewportW * kCaretLookaheadFraction;
    float xOff = static_cast<float>(center.x) + lookahead - viewportW / 2.0f;
    float yOff = static_cast<float>(center.y) - viewportH / 2.0f;

    // Clamp to virtual desktop bounds
    float minOffX = static_cast<float>(originX);
    float minOffY = static_cast<float>(originY);
    float maxOffX = static_cast<float>(originX) + static_cast<float>(screenW) - viewportW;
    float maxOffY = static_cast<float>(originY) + static_cast<float>(screenH) - viewportH;

    xOff = std::clamp(xOff, minOffX, maxOffX);
    yOff = std::clamp(yOff, minOffY, maxOffY);

    return {xOff, yOff};
}

// Priority arbitration for viewport tracking source (Doc 3 §3.4):
//   1. Caret — user is actively typing (keyboard input within 500ms) and caret available
//   2. Focus — a focus change occurred more recently than mouse movement, debounced 100ms
//   3. Pointer — default fallback
TrackingSource ViewportTracker::determineActiveSource(
    int64_t now,
    int64_t lastPointerMoveTime,
    int64_t lastFocusChangeTime,
    int64_t lastKeyboardInputTime,
    bool focusRectValid,
    bool caretRectValid) const
{
    // Caret takes priority while user is actively typing (AC-2.6.07)
    if (caretRectValid && lastKeyboardInputTime > 0 &&
        (now - lastKeyboardInputTime) < kCaretIdleTimeoutMs)
    {
        return TrackingSource::Caret;
    }

    // Focus takes priority if a focus change occurred after the last pointer move,
    // the focus rect is valid, and the debounce window has elapsed (AC-2.5.07, AC-2.5.08).
    // The debounce prevents chasing intermediate elements during rapid Tab cycling.
    if (focusRectValid && lastFocusChangeTime > 0 &&
        lastFocusChangeTime > lastPointerMoveTime &&
        (now - lastFocusChangeTime) >= kFocusDebounceMs)
    {
        return TrackingSource::Focus;
    }

    // Default: pointer tracking (AC-2.5.10)
    return TrackingSource::Pointer;
}

} // namespace SmoothZoom

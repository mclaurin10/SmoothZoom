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

    // Clamp to valid offset range (viewport cannot pan past virtual desktop edges).
    // The screen shows virtual columns [off + originX/Z, off + (originX+screenW)/Z);
    // these must stay within [originX, originX+screenW]:
    //   off >= originX * (1 - 1/Z)  and  off <= (originX+screenW) * (1 - 1/Z)
    // Same derivation as computeElementOffset below. With originX=0 this reduces
    // to the classic [0, screenW*(1-1/Z)]; with a negative origin (monitor left
    // of primary) the previous [originX, originX+screenW*(1-1/Z)] range was too
    // small by |originX|/Z and pinned tracking across the primary monitor.
    float minOffX = static_cast<float>(originX) * (1.0f - invZoom);
    float minOffY = static_cast<float>(originY) * (1.0f - invZoom);
    float maxOffX = static_cast<float>(originX + screenW) * (1.0f - invZoom);
    float maxOffY = static_cast<float>(originY + screenH) * (1.0f - invZoom);

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

    float invZoom = 1.0f / zoom;

    // Center the element on its physical monitor (AC-MM.04).
    // Derivation: we want the element at the physical center of the monitor.
    // Physical center of monitor = originX + screenW/2.
    // Zoom mapping: physX = (virtualX - offsetX) * zoom.
    // Setting physX = originX + screenW/2 and virtualX = center.x:
    //   offsetX = center.x - (originX + screenW/2) / zoom
    // When originX=0: simplifies to center.x - viewportW/2 (backward compatible).
    ScreenPoint center = elementRect.center();

    float monCenterX = static_cast<float>(originX) + static_cast<float>(screenW) / 2.0f;
    float monCenterY = static_cast<float>(originY) + static_cast<float>(screenH) / 2.0f;
    float xOff = static_cast<float>(center.x) - monCenterX / zoom;
    float yOff = static_cast<float>(center.y) - monCenterY / zoom;

    // Clamp: keep active monitor's physical display within its virtual area.
    // Active monitor physical shows virtual columns [offX + originX/Z, offX + (originX+screenW)/Z)
    // These must stay within [originX, originX+screenW]:
    //   offX >= originX * (1 - 1/Z)  and  offX <= (originX+screenW) * (1 - 1/Z)
    // When originX=0: min=0, max=screenW*(1-1/Z) — same as before.
    float minOffX = static_cast<float>(originX) * (1.0f - invZoom);
    float minOffY = static_cast<float>(originY) * (1.0f - invZoom);
    float maxOffX = static_cast<float>(originX + screenW) * (1.0f - invZoom);
    float maxOffY = static_cast<float>(originY + screenH) * (1.0f - invZoom);

    xOff = std::clamp(xOff, minOffX, maxOffX);
    yOff = std::clamp(yOff, minOffY, maxOffY);

    return {xOff, yOff};
}

// Visibility-aware focus offset (AC-2.5.05 / AC-2.5.06):
// Unlike computeElementOffset (which always centers), this keeps the viewport
// still when the focused element is already fully visible, and otherwise pans
// only far enough to reveal it (plus a small margin). Falls back to centering
// when the element cannot fit in the viewport.
ViewportTracker::Offset ViewportTracker::computeFocusOffset(
    float currentOffsetX, float currentOffsetY,
    const ScreenRect& elementRect,
    float zoom, int32_t screenW, int32_t screenH,
    int32_t originX, int32_t originY)
{
    if (zoom <= 1.0f)
        return {0.0f, 0.0f};

    const float invZoom = 1.0f / zoom;

    // Valid global-offset range that keeps this monitor's physical area within its
    // virtual region (identical derivation to computeElementOffset's clamp).
    const float minOffX = static_cast<float>(originX) * (1.0f - invZoom);
    const float minOffY = static_cast<float>(originY) * (1.0f - invZoom);
    const float maxOffX = static_cast<float>(originX + screenW) * (1.0f - invZoom);
    const float maxOffY = static_cast<float>(originY + screenH) * (1.0f - invZoom);

    // Minimal 1-D pan. The visible virtual span on this monitor is
    // [curOff + origin/zoom, curOff + (origin+extent)/zoom). If [lo,hi] is already
    // inside it, keep curOff (no pan — AC-2.5.05). If clipped, slide just enough to
    // reveal the clipped edge + margin (AC-2.5.06). If the element is larger than
    // the viewport, center it (legacy computeElementOffset behavior).
    auto solve = [invZoom](float curOff, float lo, float hi,
                           float origin, float extent,
                           float minOff, float maxOff) -> float
    {
        const float viewportSpan = extent * invZoom;       // visible virtual extent
        const float elementSpan  = hi - lo;

        if (elementSpan >= viewportSpan)                   // cannot fit — center it
        {
            const float monCenter = origin + extent * 0.5f;
            const float center = (lo + hi) * 0.5f;
            return std::clamp(center - monCenter * invZoom, minOff, maxOff);
        }

        const float visibleLo = curOff + origin * invZoom;
        const float visibleHi = curOff + (origin + extent) * invZoom;

        if (lo >= visibleLo && hi <= visibleHi)            // already visible — no pan
            return std::clamp(curOff, minOff, maxOff);

        const float margin = std::min(kFocusRevealMarginPx,
                                      (viewportSpan - elementSpan) * 0.5f);

        float newOff = curOff;
        if (lo < visibleLo)                                // clipped on the low edge
            newOff = lo - margin - origin * invZoom;
        else                                               // clipped on the high edge
            newOff = hi + margin - (origin + extent) * invZoom;

        return std::clamp(newOff, minOff, maxOff);
    };

    const float xOff = solve(currentOffsetX,
                             static_cast<float>(elementRect.left),
                             static_cast<float>(elementRect.right),
                             static_cast<float>(originX),
                             static_cast<float>(screenW), minOffX, maxOffX);
    const float yOff = solve(currentOffsetY,
                             static_cast<float>(elementRect.top),
                             static_cast<float>(elementRect.bottom),
                             static_cast<float>(originY),
                             static_cast<float>(screenH), minOffY, maxOffY);

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

    float invZoom = 1.0f / zoom;
    float viewportW = static_cast<float>(screenW) / zoom;

    ScreenPoint center = caretRect.center();

    // Center on physical monitor + lookahead (AC-2.6.06, AC-MM.04)
    float monCenterX = static_cast<float>(originX) + static_cast<float>(screenW) / 2.0f;
    float monCenterY = static_cast<float>(originY) + static_cast<float>(screenH) / 2.0f;
    float lookahead = viewportW * kCaretLookaheadFraction;
    float xOff = static_cast<float>(center.x) + lookahead - monCenterX / zoom;
    float yOff = static_cast<float>(center.y) - monCenterY / zoom;

    // Clamp: keep active monitor's physical display within its virtual area
    float minOffX = static_cast<float>(originX) * (1.0f - invZoom);
    float minOffY = static_cast<float>(originY) * (1.0f - invZoom);
    float maxOffX = static_cast<float>(originX + screenW) * (1.0f - invZoom);
    float maxOffY = static_cast<float>(originY + screenH) * (1.0f - invZoom);

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

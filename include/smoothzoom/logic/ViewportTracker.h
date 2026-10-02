#pragma once
// =============================================================================
// SmoothZoom — ViewportTracker
// Offset computation, proportional pointer mapping, priority arbitration.
// Doc 3 §3.6
// =============================================================================

#include "smoothzoom/common/Types.h"

namespace SmoothZoom
{

class ViewportTracker
{
public:
    struct Offset
    {
        float x = 0.0f;
        float y = 0.0f;
    };

    // Core proportional mapping: desktopX under pointer == pointerX (Doc 3 §3.6)
    // originX/originY: virtual screen origin (SM_XVIRTUALSCREEN, SM_YVIRTUALSCREEN)
    static Offset computePointerOffset(int32_t pointerX, int32_t pointerY,
                                        float zoom, int32_t screenW, int32_t screenH,
                                        int32_t originX = 0, int32_t originY = 0);

    // Element-center offset (for focus/caret tracking)
    static Offset computeElementOffset(const ScreenRect& elementRect,
                                        float zoom, int32_t screenW, int32_t screenH,
                                        int32_t originX = 0, int32_t originY = 0);

    // Visibility-aware focus offset (AC-2.5.05 / AC-2.5.06): given the CURRENT
    // viewport offset, returns it unchanged when the element is already fully
    // visible (no pan), pans just far enough (+margin) when partially clipped, and
    // only falls back to centering when the element is larger than the viewport.
    static Offset computeFocusOffset(float currentOffsetX, float currentOffsetY,
                                     const ScreenRect& elementRect,
                                     float zoom, int32_t screenW, int32_t screenH,
                                     int32_t originX = 0, int32_t originY = 0);

    // Anchor snap-back reduction (R-23, AC-2.1.09–11). The API takes whole-pixel
    // offsets, so the desktop point under the pointer sits up to 0.5 desktop px
    // (0.5 x zoom screen px) from where proportional mapping wants it, and while
    // the zoom moves that error jumps back and forth. While the zoom is moving
    // from prevZoom to zoom, this picks the zoom to display: within
    // kAnchorNudgeMax of `zoom`, never ahead of it, and at least
    // kAnchorMinStepFraction of the way from prevZoom (so the zoom stays
    // monotone and keeps an even pace). It prefers the candidate whose on-screen
    // anchor error (zoom * (ideal - round(ideal)), per axis) moves less than
    // 1 px from prevErr; a larger move is cheap if it continues the axis's last
    // >= 1 px move (prevDir, -1/0/+1) and expensive if it reverses it (a
    // snap-back); then the smallest error, then the smallest nudge.
    // Offsets are rounded with lround, as MagBridge does.
    struct AnchorAlign
    {
        float zoom = 1.0f;
        float errX = 0.0f;   // on-screen anchor error at that zoom, physical px
        float errY = 0.0f;
    };
    static AnchorAlign alignZoomForAnchor(float zoom, float prevZoom,
                                          bool havePrevErr, float prevErrX, float prevErrY,
                                          int32_t pointerX, int32_t pointerY,
                                          int32_t screenW, int32_t screenH,
                                          int32_t originX = 0, int32_t originY = 0,
                                          int prevDirX = 0, int prevDirY = 0);

    // On-screen anchor error for the pointer at this zoom with rounded offsets.
    static AnchorAlign anchorError(float zoom, int32_t pointerX, int32_t pointerY,
                                   int32_t screenW, int32_t screenH,
                                   int32_t originX = 0, int32_t originY = 0);

    static constexpr float kAnchorNudgeMax = 0.005f;        // 0.5 % of the zoom
    static constexpr float kAnchorMinStepFraction = 0.5f;   // of this frame's zoom step

    // Determine active tracking source based on timestamps and priorities (Doc 3 §3.4)
    // Priority: Caret (if typing within caretIdleMs) > Focus (if recent, debounced) > Pointer
    TrackingSource determineActiveSource(int64_t now,
                                          int64_t lastPointerMoveTime,
                                          int64_t lastFocusChangeTime,
                                          int64_t lastKeyboardInputTime,
                                          bool focusRectValid,
                                          bool caretRectValid) const;

    // Caret offset with lookahead margin (AC-2.6.06)
    // Shifts target ahead of caret in typing direction so user can see upcoming text.
    static Offset computeCaretOffset(const ScreenRect& caretRect,
                                      float zoom, int32_t screenW, int32_t screenH,
                                      int32_t originX = 0, int32_t originY = 0);

    // Tunable thresholds (milliseconds)
    static constexpr int64_t kCaretIdleTimeoutMs = 500;   // AC-2.6.07: caret priority while typing
    static constexpr int64_t kFocusDebounceMs    = 100;   // AC-2.5.07: debounce rapid focus changes

    // Lookahead margin: fraction of viewport width ahead of caret (AC-2.6.06)
    static constexpr float kCaretLookaheadFraction = 0.15f; // ~15% of viewport width

    // Reveal margin when panning a clipped focus element into view (AC-2.5.06).
    // Virtual (pre-zoom) pixels kept between the element edge and the screen edge.
    static constexpr float kFocusRevealMarginPx = 40.0f;
};

} // namespace SmoothZoom

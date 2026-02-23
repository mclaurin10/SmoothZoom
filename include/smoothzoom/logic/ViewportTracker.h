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
    static Offset computePointerOffset(int32_t pointerX, int32_t pointerY,
                                        float zoom, int32_t screenW, int32_t screenH);

    // Element-center offset (for focus/caret tracking)
    static Offset computeElementOffset(const ScreenRect& elementRect,
                                        float zoom, int32_t screenW, int32_t screenH);

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
                                      float zoom, int32_t screenW, int32_t screenH);

    // Tunable thresholds (milliseconds)
    static constexpr int64_t kCaretIdleTimeoutMs = 500;   // AC-2.6.07: caret priority while typing
    static constexpr int64_t kFocusDebounceMs    = 100;   // AC-2.5.07: debounce rapid focus changes

    // Lookahead margin: fraction of viewport width ahead of caret (AC-2.6.06)
    static constexpr float kCaretLookaheadFraction = 0.15f; // ~15% of viewport width
};

} // namespace SmoothZoom

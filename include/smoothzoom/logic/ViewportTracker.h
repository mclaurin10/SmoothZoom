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

    // Determine active tracking source based on timestamps and priorities
    TrackingSource determineActiveSource(int64_t lastPointerMoveTime,
                                          int64_t lastFocusChangeTime,
                                          int64_t lastKeyboardInputTime) const;
};

} // namespace SmoothZoom

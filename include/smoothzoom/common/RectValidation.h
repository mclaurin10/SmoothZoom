#pragma once
// =============================================================================
// SmoothZoom — Rectangle Validation
// Shared, pure (no Win32) bounds check for UIA / GTTI rectangles. (R-09)
//
// Replaces hard-coded magic-number limits (±5000 / 10000) that wrongly rejected
// valid focus/caret rects on large or left/above multi-monitor layouts (the
// virtual desktop can have a negative origin and a large extent). Validates
// against the LIVE virtual-desktop bounds instead.
//
// Header-only and allocation-free — usable on the render hot path (RenderLoop)
// as well as the UIA / Caret threads.
// =============================================================================

#include <cstdint>

namespace SmoothZoom
{

// Margin (px) added around the virtual desktop to tolerate edge / DPI rounding
// and slightly-out-of-bounds-but-real rectangles, while still rejecting the
// classic UIA garbage (huge or far-off-screen rects). (R-09)
inline constexpr int32_t kVirtualDesktopMargin = 256;

// True if the rectangle [left,top,right,bottom] intersects the virtual desktop
// [vx, vy] .. [vx + vw, vy + vh], inflated by kVirtualDesktopMargin.
//
// vw <= 0 || vh <= 0 means the bounds are not yet populated (very early
// startup) — return true so we never over-reject before the metrics exist.
// int64 arithmetic avoids int32 overflow on 8K-class virtual desktops near a
// large origin. Only rects lying ENTIRELY outside the inflated desktop are
// rejected; a partially on-screen rect is kept (viewport math clamps later).
inline bool rectIntersectsVirtualDesktop(int32_t left, int32_t top,
                                         int32_t right, int32_t bottom,
                                         int32_t vx, int32_t vy,
                                         int32_t vw, int32_t vh)
{
    if (vw <= 0 || vh <= 0)
        return true; // bounds not yet known — do not over-reject

    const int64_t boundsLeft   = static_cast<int64_t>(vx) - kVirtualDesktopMargin;
    const int64_t boundsTop    = static_cast<int64_t>(vy) - kVirtualDesktopMargin;
    const int64_t boundsRight  = static_cast<int64_t>(vx) + vw + kVirtualDesktopMargin;
    const int64_t boundsBottom = static_cast<int64_t>(vy) + vh + kVirtualDesktopMargin;

    if (right < boundsLeft || left > boundsRight)
        return false;
    if (bottom < boundsTop || top > boundsBottom)
        return false;
    return true;
}

} // namespace SmoothZoom

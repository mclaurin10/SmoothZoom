// =============================================================================
// Unit tests — RectValidation (rectIntersectsVirtualDesktop)
//
// Regression coverage for the hard-coded ±5000 / 10000 UIA limits that wrongly
// rejected valid focus/caret rects on large or negative-origin multi-monitor
// layouts. The fix validates against the LIVE virtual-desktop bounds instead.
// =============================================================================

#include <catch2/catch_test_macros.hpp>
#include "smoothzoom/common/RectValidation.h"

using namespace SmoothZoom;

TEST_CASE("Rect on the primary monitor is accepted", "[rectvalidation]")
{
    // Primary 1920x1080 at origin (0,0).
    REQUIRE(rectIntersectsVirtualDesktop(100, 100, 200, 200, 0, 0, 1920, 1080));
}

TEST_CASE("Negative-origin left monitor is accepted (old ±5000 limit regression)",
          "[rectvalidation]")
{
    // A 5120px monitor placed left of primary → virtual origin x = -5120.
    const int32_t vx = -5120, vy = 0, vw = 5120 + 1920, vh = 1080;

    // A focus rect on that left monitor at left = -5100. The old heuristic
    // rejected anything with left < -5000; the bounds-aware check accepts it.
    REQUIRE(rectIntersectsVirtualDesktop(-5100, 100, -5000, 200, vx, vy, vw, vh));
}

TEST_CASE("Monitor stacked above primary is accepted", "[rectvalidation]")
{
    // A 1440px-tall monitor above primary → virtual origin y = -1440. A rect
    // near the top of that monitor (top = -1400) was rejected by top < -5000?
    // No — but a taller stack (two 4K-portrait) pushes top below -5000.
    const int32_t vx = 0, vy = -6000, vw = 1920, vh = 6000 + 1080;
    REQUIRE(rectIntersectsVirtualDesktop(100, -5900, 200, -5800, vx, vy, vw, vh));
}

TEST_CASE("Wide element exceeding the old 10000 size cap is accepted",
          "[rectvalidation]")
{
    // Three 3840px monitors side by side → 11520px wide virtual desktop.
    const int32_t vx = 0, vy = 0, vw = 11520, vh = 1080;
    // A full-width element (11000px) tripped the old w > 10000 reject.
    REQUIRE(rectIntersectsVirtualDesktop(0, 0, 11000, 500, vx, vy, vw, vh));
}

TEST_CASE("Rect far off the virtual desktop is rejected", "[rectvalidation]")
{
    // Garbage rect well beyond the inflated desktop bounds.
    REQUIRE_FALSE(rectIntersectsVirtualDesktop(100000, 100000, 100100, 100200,
                                               0, 0, 1920, 1080));
    // Far negative junk.
    REQUIRE_FALSE(rectIntersectsVirtualDesktop(-50000, -50000, -49900, -49800,
                                               0, 0, 1920, 1080));
}

TEST_CASE("Margin keeps slightly-out-of-bounds rects, rejects beyond margin",
          "[rectvalidation]")
{
    const int32_t vx = 0, vy = 0, vw = 1920, vh = 1080;
    // Just outside the right edge but within kVirtualDesktopMargin → kept.
    REQUIRE(rectIntersectsVirtualDesktop(1920 + 10, 100, 1920 + 100, 200,
                                         vx, vy, vw, vh));
    // Beyond the margin → rejected.
    REQUIRE_FALSE(rectIntersectsVirtualDesktop(1920 + kVirtualDesktopMargin + 1, 100,
                                               1920 + kVirtualDesktopMargin + 50, 200,
                                               vx, vy, vw, vh));
}

TEST_CASE("Unpopulated bounds do not over-reject", "[rectvalidation]")
{
    // Before WM_DISPLAYCHANGE/startup populates the metrics (vw/vh == 0),
    // any rect must be treated as valid to avoid rejecting everything.
    REQUIRE(rectIntersectsVirtualDesktop(100, 100, 200, 200, 0, 0, 0, 0));
    REQUIRE(rectIntersectsVirtualDesktop(-9999, -9999, 9999, 9999, 0, 0, 0, 0));
}

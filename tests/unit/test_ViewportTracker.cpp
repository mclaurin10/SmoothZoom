// =============================================================================
// Unit tests for ViewportTracker — Doc 3 §3.4
// Pure logic — no Win32 API dependencies.
// =============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "smoothzoom/logic/ViewportTracker.h"

using namespace SmoothZoom;
using Catch::Approx;

// Standard test screen: 1920x1080
static constexpr int kScreenW = 1920;
static constexpr int kScreenH = 1080;

// ─── Proportional mapping ───────────────────────────────────────────────────

TEST_CASE("At 1.0x zoom, offset is (0,0)", "[ViewportTracker]")
{
    auto off = ViewportTracker::computePointerOffset(960, 540, 1.0f, kScreenW, kScreenH);
    REQUIRE(off.x == Approx(0.0f));
    REQUIRE(off.y == Approx(0.0f));
}

TEST_CASE("Pointer at center produces centered offset", "[ViewportTracker]")
{
    // At 2x with pointer at center (960, 540):
    // xOff = 960 * (1 - 1/2) = 960 * 0.5 = 480
    // yOff = 540 * (1 - 1/2) = 540 * 0.5 = 270
    auto off = ViewportTracker::computePointerOffset(960, 540, 2.0f, kScreenW, kScreenH);
    REQUIRE(off.x == Approx(480.0f));
    REQUIRE(off.y == Approx(270.0f));
}

TEST_CASE("Pointer at top-left corner gives (0,0) offset", "[ViewportTracker]")
{
    auto off = ViewportTracker::computePointerOffset(0, 0, 2.0f, kScreenW, kScreenH);
    REQUIRE(off.x == Approx(0.0f));
    REQUIRE(off.y == Approx(0.0f));
}

TEST_CASE("Pointer at bottom-right gives max offset", "[ViewportTracker]")
{
    // At 2x, max offset = screenDim * (1 - 1/2) = screenDim * 0.5
    auto off = ViewportTracker::computePointerOffset(kScreenW, kScreenH, 2.0f, kScreenW, kScreenH);
    REQUIRE(off.x == Approx(960.0f));
    REQUIRE(off.y == Approx(540.0f));
}

TEST_CASE("Desktop point under pointer equals pointer position (Doc 3 §3.4 key property)",
          "[ViewportTracker]")
{
    // The proportional mapping formula guarantees:
    //   desktopX = xOffset + pointerX / zoom == pointerX
    int px = 700, py = 400;
    float zoom = 3.5f;
    auto off = ViewportTracker::computePointerOffset(px, py, zoom, kScreenW, kScreenH);

    float desktopX = off.x + static_cast<float>(px) / zoom;
    float desktopY = off.y + static_cast<float>(py) / zoom;

    REQUIRE(desktopX == Approx(static_cast<float>(px)).margin(0.1f));
    REQUIRE(desktopY == Approx(static_cast<float>(py)).margin(0.1f));
}

TEST_CASE("Offset scales with zoom level", "[ViewportTracker]")
{
    int px = 960, py = 540; // center
    auto off2x = ViewportTracker::computePointerOffset(px, py, 2.0f, kScreenW, kScreenH);
    auto off5x = ViewportTracker::computePointerOffset(px, py, 5.0f, kScreenW, kScreenH);
    auto off10x = ViewportTracker::computePointerOffset(px, py, 10.0f, kScreenW, kScreenH);

    // Higher zoom = larger offset
    REQUIRE(off5x.x > off2x.x);
    REQUIRE(off10x.x > off5x.x);
}

// ─── Edge clamping ──────────────────────────────────────────────────────────

TEST_CASE("Offset clamped to non-negative", "[ViewportTracker]")
{
    // Even with pointer at (0,0), offset should be >= 0
    auto off = ViewportTracker::computePointerOffset(-10, -10, 2.0f, kScreenW, kScreenH);
    REQUIRE(off.x >= 0.0f);
    REQUIRE(off.y >= 0.0f);
}

TEST_CASE("Offset clamped to maximum", "[ViewportTracker]")
{
    // Pointer way past screen edge
    auto off = ViewportTracker::computePointerOffset(5000, 5000, 2.0f, kScreenW, kScreenH);
    float maxX = kScreenW * (1.0f - 1.0f / 2.0f);
    float maxY = kScreenH * (1.0f - 1.0f / 2.0f);
    REQUIRE(off.x <= maxX + 0.01f);
    REQUIRE(off.y <= maxY + 0.01f);
}

// ─── Element offset (focus/caret tracking) ──────────────────────────────────

TEST_CASE("Element offset centers element in viewport", "[ViewportTracker]")
{
    ScreenRect rect{800, 400, 900, 450}; // 100x50 element at (800,400)
    auto off = ViewportTracker::computeElementOffset(rect, 2.0f, kScreenW, kScreenH);

    // The element center is (850, 425)
    // Viewport size at 2x = (960, 540)
    // Desired offset: centerX - viewportW/2 = 850 - 480 = 370
    //                 centerY - viewportH/2 = 425 - 270 = 155
    REQUIRE(off.x == Approx(370.0f));
    REQUIRE(off.y == Approx(155.0f));
}

TEST_CASE("Element offset clamps at edges", "[ViewportTracker]")
{
    // Element near top-left
    ScreenRect rect{10, 10, 50, 30};
    auto off = ViewportTracker::computeElementOffset(rect, 2.0f, kScreenW, kScreenH);
    REQUIRE(off.x >= 0.0f);
    REQUIRE(off.y >= 0.0f);
}

// ─── Tracking source priority ───────────────────────────────────────────────

TEST_CASE("Default tracking source is Pointer", "[ViewportTracker]")
{
    ViewportTracker vt;
    // Phase 0–2: always returns Pointer
    auto src = vt.determineActiveSource(0, 0, 0);
    REQUIRE(src == TrackingSource::Pointer);
}

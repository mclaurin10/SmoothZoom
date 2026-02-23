// =============================================================================
// Unit tests for ViewportTracker — Doc 3 §3.6
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

// ─── Corner reachability (AC-2.4.05) ─────────────────────────────────────

TEST_CASE("All four corners reachable at 5x zoom (AC-2.4.05)", "[ViewportTracker]")
{
    float zoom = 5.0f;
    float viewW = kScreenW / zoom;
    float viewH = kScreenH / zoom;

    // Top-left corner: pointer at (0,0)
    auto tl = ViewportTracker::computePointerOffset(0, 0, zoom, kScreenW, kScreenH);
    REQUIRE(tl.x == Approx(0.0f));
    REQUIRE(tl.y == Approx(0.0f));

    // Bottom-right corner: pointer at screen edge
    auto br = ViewportTracker::computePointerOffset(kScreenW, kScreenH, zoom, kScreenW, kScreenH);
    REQUIRE(br.x == Approx(kScreenW - viewW).margin(1.0f));
    REQUIRE(br.y == Approx(kScreenH - viewH).margin(1.0f));

    // Top-right corner
    auto tr = ViewportTracker::computePointerOffset(kScreenW, 0, zoom, kScreenW, kScreenH);
    REQUIRE(tr.x == Approx(kScreenW - viewW).margin(1.0f));
    REQUIRE(tr.y == Approx(0.0f));

    // Bottom-left corner
    auto bl = ViewportTracker::computePointerOffset(0, kScreenH, zoom, kScreenW, kScreenH);
    REQUIRE(bl.x == Approx(0.0f));
    REQUIRE(bl.y == Approx(kScreenH - viewH).margin(1.0f));
}

// ─── High zoom tracking (AC-2.4.12, AC-2.4.13) ──────────────────────────

TEST_CASE("Proportional tracking works at 10x zoom (AC-2.4.12)", "[ViewportTracker]")
{
    float zoom = 10.0f;
    int px = 960, py = 540;
    auto off = ViewportTracker::computePointerOffset(px, py, zoom, kScreenW, kScreenH);

    // Verify key property holds at extreme zoom
    float desktopX = off.x + static_cast<float>(px) / zoom;
    float desktopY = off.y + static_cast<float>(py) / zoom;

    REQUIRE(desktopX == Approx(static_cast<float>(px)).margin(0.1f));
    REQUIRE(desktopY == Approx(static_cast<float>(py)).margin(0.1f));
}

// ─── Tracking source priority (Phase 3: AC-2.5.x, AC-2.6.x) ────────────

// Helper constants matching ViewportTracker thresholds
static constexpr int64_t kCaretIdle = ViewportTracker::kCaretIdleTimeoutMs;  // 500ms
static constexpr int64_t kFocusDebounce = ViewportTracker::kFocusDebounceMs; // 100ms

TEST_CASE("Default tracking source is Pointer when no input events", "[ViewportTracker][Phase3]")
{
    ViewportTracker vt;
    // No events at all → Pointer
    auto src = vt.determineActiveSource(1000, 0, 0, 0, false, false);
    REQUIRE(src == TrackingSource::Pointer);
}

TEST_CASE("Pointer when focus/caret rects are invalid", "[ViewportTracker][Phase3]")
{
    ViewportTracker vt;
    // Even with recent keyboard input, invalid caret rect → no caret source
    auto src = vt.determineActiveSource(1000, 500, 600, 900, false, false);
    REQUIRE(src == TrackingSource::Pointer);
}

// ─── Caret priority (AC-2.6.07) ─────────────────────────────────────────

TEST_CASE("Caret active when typing within 500ms and caret valid (AC-2.6.07)",
          "[ViewportTracker][Phase3]")
{
    ViewportTracker vt;
    int64_t now = 10000;
    int64_t lastKb = now - 200; // 200ms ago — within 500ms threshold

    auto src = vt.determineActiveSource(now, 5000, 6000, lastKb, true, true);
    REQUIRE(src == TrackingSource::Caret);
}

TEST_CASE("Caret inactive after 500ms idle — handoff to pointer (AC-2.6.08)",
          "[ViewportTracker][Phase3]")
{
    ViewportTracker vt;
    int64_t now = 10000;
    int64_t lastKb = now - kCaretIdle; // Exactly at boundary → should NOT be caret

    auto src = vt.determineActiveSource(now, 9000, 6000, lastKb, true, true);
    REQUIRE(src == TrackingSource::Pointer);
}

TEST_CASE("Caret inactive when caret rect invalid even if typing", "[ViewportTracker][Phase3]")
{
    ViewportTracker vt;
    int64_t now = 10000;
    int64_t lastKb = now - 100; // Recent typing

    auto src = vt.determineActiveSource(now, 5000, 6000, lastKb, true, false);
    // Caret unavailable, focus change was before pointer move → focus if debounced
    // lastFocusChange=6000, lastPointerMove=5000 → focus > pointer
    // now - lastFocus = 4000ms > 100ms debounce → FOCUS
    REQUIRE(src == TrackingSource::Focus);
}

// ─── Focus priority (AC-2.5.01, AC-2.5.07, AC-2.5.10–12) ──────────────

TEST_CASE("Focus active when focus change is more recent than pointer move (AC-2.5.12)",
          "[ViewportTracker][Phase3]")
{
    ViewportTracker vt;
    int64_t now = 10000;
    int64_t lastPointer = 5000;
    int64_t lastFocus = 9000; // Focus after pointer move
    int64_t lastKb = 0;       // No typing

    // Focus change was 1000ms ago, debounce (100ms) passed
    auto src = vt.determineActiveSource(now, lastPointer, lastFocus, lastKb, true, false);
    REQUIRE(src == TrackingSource::Focus);
}

TEST_CASE("Focus debounce: no focus source within 100ms of focus change (AC-2.5.07)",
          "[ViewportTracker][Phase3]")
{
    ViewportTracker vt;
    int64_t now = 10000;
    int64_t lastPointer = 5000;
    int64_t lastFocus = now - 50; // Focus changed 50ms ago — within debounce

    auto src = vt.determineActiveSource(now, lastPointer, lastFocus, 0, true, false);
    // Debounce not elapsed → fall through to Pointer
    REQUIRE(src == TrackingSource::Pointer);
}

TEST_CASE("Focus debounce: source activates after 100ms (AC-2.5.08)",
          "[ViewportTracker][Phase3]")
{
    ViewportTracker vt;
    int64_t now = 10000;
    int64_t lastPointer = 5000;
    int64_t lastFocus = now - kFocusDebounce; // Exactly at debounce boundary → should activate

    auto src = vt.determineActiveSource(now, lastPointer, lastFocus, 0, true, false);
    REQUIRE(src == TrackingSource::Focus);
}

TEST_CASE("Pointer resumes when mouse moves after focus pan (AC-2.5.11)",
          "[ViewportTracker][Phase3]")
{
    ViewportTracker vt;
    int64_t now = 10000;
    int64_t lastFocus = 8000;
    int64_t lastPointer = 9000; // Mouse moved AFTER focus change

    auto src = vt.determineActiveSource(now, lastPointer, lastFocus, 0, true, false);
    // lastPointerMove > lastFocusChange → Pointer wins
    REQUIRE(src == TrackingSource::Pointer);
}

TEST_CASE("Focus inactive when focus rect is invalid (AC-2.5.14)", "[ViewportTracker][Phase3]")
{
    ViewportTracker vt;
    int64_t now = 10000;

    auto src = vt.determineActiveSource(now, 5000, 9000, 0, false, false);
    REQUIRE(src == TrackingSource::Pointer);
}

// ─── Priority ordering: Caret > Focus > Pointer ─────────────────────────

TEST_CASE("Caret takes priority over Focus when both valid (AC-2.6.09)",
          "[ViewportTracker][Phase3]")
{
    ViewportTracker vt;
    int64_t now = 10000;
    int64_t lastPointer = 5000;
    int64_t lastFocus = 9000;   // Recent focus change
    int64_t lastKb = now - 100; // Recent typing

    // Both focus and caret valid — caret wins because typing is active
    auto src = vt.determineActiveSource(now, lastPointer, lastFocus, lastKb, true, true);
    REQUIRE(src == TrackingSource::Caret);
}

TEST_CASE("Tab during typing switches to Focus (AC-2.6.09)", "[ViewportTracker][Phase3]")
{
    ViewportTracker vt;
    int64_t now = 10000;
    // Scenario: user was typing, then pressed Tab → focus changed more recently than pointer
    // After caret idle timeout (500ms), focus takes over
    int64_t lastKb = now - 600;   // Last typing was 600ms ago (past caret timeout)
    int64_t lastPointer = 5000;
    int64_t lastFocus = now - 200; // Tab press caused focus change 200ms ago (past debounce)

    auto src = vt.determineActiveSource(now, lastPointer, lastFocus, lastKb, true, true);
    REQUIRE(src == TrackingSource::Focus);
}

// ─── Edge cases ──────────────────────────────────────────────────────────

TEST_CASE("All timestamps zero returns Pointer", "[ViewportTracker][Phase3]")
{
    ViewportTracker vt;
    auto src = vt.determineActiveSource(0, 0, 0, 0, true, true);
    REQUIRE(src == TrackingSource::Pointer);
}

TEST_CASE("Caret with keyboard timestamp zero returns Pointer (no typing detected)",
          "[ViewportTracker][Phase3]")
{
    ViewportTracker vt;
    auto src = vt.determineActiveSource(10000, 5000, 6000, 0, true, true);
    // lastKb=0, so caret check fails (lastKb > 0 required)
    // Focus: lastFocus=6000 > lastPointer=5000, debounce=4000ms > 100ms → FOCUS
    REQUIRE(src == TrackingSource::Focus);
}

// ─── Caret lookahead (AC-2.6.06) ────────────────────────────────────────

TEST_CASE("Caret offset includes lookahead margin (AC-2.6.06)", "[ViewportTracker][Phase3]")
{
    // Caret at center of screen, 2x zoom
    ScreenRect caret{960, 530, 962, 550}; // Thin caret at ~center
    auto caretOff = ViewportTracker::computeCaretOffset(caret, 2.0f, kScreenW, kScreenH);
    auto elemOff  = ViewportTracker::computeElementOffset(caret, 2.0f, kScreenW, kScreenH);

    // Caret offset should be shifted right (positive X) by the lookahead
    // Lookahead = (1920/2) * 0.15 = 144 pixels in viewport coords
    REQUIRE(caretOff.x > elemOff.x);

    // The shift should be approximately 15% of viewport width
    float viewportW = kScreenW / 2.0f; // 960 at 2x
    float expectedShift = viewportW * ViewportTracker::kCaretLookaheadFraction;
    REQUIRE(caretOff.x == Approx(elemOff.x + expectedShift).margin(1.0f));
}

TEST_CASE("Caret lookahead clamps at right edge", "[ViewportTracker][Phase3]")
{
    // Caret near right edge of screen — lookahead would push past max offset
    ScreenRect caret{1900, 540, 1902, 560};
    auto off = ViewportTracker::computeCaretOffset(caret, 2.0f, kScreenW, kScreenH);

    // Should be clamped to max offset
    float maxOffX = kScreenW * (1.0f - 1.0f / 2.0f); // 960
    REQUIRE(off.x <= maxOffX + 0.01f);
}

TEST_CASE("Caret offset at 1.0x returns (0,0)", "[ViewportTracker][Phase3]")
{
    ScreenRect caret{500, 300, 502, 320};
    auto off = ViewportTracker::computeCaretOffset(caret, 1.0f, kScreenW, kScreenH);
    REQUIRE(off.x == Approx(0.0f));
    REQUIRE(off.y == Approx(0.0f));
}

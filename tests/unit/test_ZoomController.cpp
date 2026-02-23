// =============================================================================
// Unit tests for ZoomController — Doc 3 §3.5
// Pure logic — no Win32 API dependencies.
// =============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "smoothzoom/logic/ZoomController.h"
#include <cmath>

using namespace SmoothZoom;
using Catch::Approx;

TEST_CASE("ZoomController starts at 1.0x idle", "[ZoomController]")
{
    ZoomController zc;
    REQUIRE(zc.currentZoom() == Approx(1.0f));
    REQUIRE(zc.targetZoom() == Approx(1.0f));
    REQUIRE(zc.mode() == ZoomController::Mode::Idle);
}

TEST_CASE("Scroll delta zooms in", "[ZoomController]")
{
    ZoomController zc;

    // One notch up (120 units): logarithmic model → zoom *= 1.1
    zc.applyScrollDelta(120);
    REQUIRE(zc.currentZoom() > 1.0f);
    REQUIRE(zc.currentZoom() == Approx(1.1f));
    REQUIRE(zc.mode() == ZoomController::Mode::Scrolling);
}

TEST_CASE("Scroll delta zooms out", "[ZoomController]")
{
    ZoomController zc;

    // First zoom in
    zc.applyScrollDelta(120);
    float zoomed = zc.currentZoom();

    // Then zoom out
    zc.applyScrollDelta(-120);
    REQUIRE(zc.currentZoom() < zoomed);
}

TEST_CASE("Zoom clamps to minimum 1.0x", "[ZoomController]")
{
    ZoomController zc;

    // Scroll down from 1.0× — should stay at 1.0× (soft approach + hard clamp)
    zc.applyScrollDelta(-120);
    REQUIRE(zc.currentZoom() == Approx(1.0f));
}

TEST_CASE("Zoom clamps to maximum 10.0x", "[ZoomController]")
{
    ZoomController zc;

    // Apply massive scroll up — soft approach decelerates, hard clamp at 10.0
    for (int i = 0; i < 200; ++i)
        zc.applyScrollDelta(120);

    REQUIRE(zc.currentZoom() <= 10.0f);
}

TEST_CASE("Zoom snaps to 1.0x within epsilon (R-17)", "[ZoomController]")
{
    ZoomController zc;

    // Zoom in slightly
    zc.applyScrollDelta(5); // Small delta — might land near 1.0
    // If the result is within 0.005 of 1.0, it should snap to exactly 1.0
    float zoom = zc.currentZoom();
    if (zoom < 1.005f && zoom > 0.995f)
    {
        REQUIRE(zoom == Approx(1.0f));
    }
}

TEST_CASE("Zero scroll delta is a no-op", "[ZoomController]")
{
    ZoomController zc;
    zc.applyScrollDelta(0);
    REQUIRE(zc.currentZoom() == Approx(1.0f));
    REQUIRE(zc.mode() == ZoomController::Mode::Idle);
}

TEST_CASE("Reset returns to 1.0x idle", "[ZoomController]")
{
    ZoomController zc;

    zc.applyScrollDelta(120);
    REQUIRE(zc.currentZoom() > 1.0f);

    zc.reset();
    REQUIRE(zc.currentZoom() == Approx(1.0f));
    REQUIRE(zc.targetZoom() == Approx(1.0f));
    REQUIRE(zc.mode() == ZoomController::Mode::Idle);
}

TEST_CASE("Logarithmic model: equal effort for equal ratios (AC-2.1.06)", "[ZoomController]")
{
    // Core property: 1×→2× requires same scroll as 5×→10× (same ratio = 2:1)
    // With pow(1.1, n) model: zoom_after = zoom_before * 1.1^n
    // So n = log(ratio) / log(1.1) regardless of starting zoom.

    ZoomController zc1;
    // Count notches from 1.0 to 2.0
    int notches_1_to_2 = 0;
    while (zc1.currentZoom() < 2.0f && notches_1_to_2 < 200)
    {
        zc1.applyScrollDelta(120);
        ++notches_1_to_2;
    }

    ZoomController zc2;
    // Get to 5.0× first
    while (zc2.currentZoom() < 5.0f)
        zc2.applyScrollDelta(120);

    // Count notches from 5.0 to 10.0
    int notches_5_to_10 = 0;
    float start = zc2.currentZoom();
    float target = start * 2.0f; // same 2:1 ratio
    while (zc2.currentZoom() < target && notches_5_to_10 < 200)
    {
        zc2.applyScrollDelta(120);
        ++notches_5_to_10;
    }

    // Should be approximately equal (within 1–2 notches due to soft bounds near 10×)
    REQUIRE(std::abs(notches_1_to_2 - notches_5_to_10) <= 2);
}

TEST_CASE("Multiple scroll deltas accumulate correctly", "[ZoomController]")
{
    ZoomController zc;

    // Apply 3 notches in a single call
    zc.applyScrollDelta(360); // 3 * 120
    float threeNotchSingle = zc.currentZoom();

    // Both should be >1.0 and reasonable
    REQUIRE(threeNotchSingle > 1.0f);

    // 3 notches at once = 1.0 * 1.1^3 ≈ 1.331
    REQUIRE(threeNotchSingle == Approx(std::pow(1.1f, 3.0f)).margin(0.01f));
}

TEST_CASE("Sub-notch precision touchpad delta works (AC-2.1.05)", "[ZoomController]")
{
    ZoomController zc;

    // Precision touchpad sends sub-120 deltas
    zc.applyScrollDelta(30); // Quarter notch → 1.0 * 1.1^0.25
    REQUIRE(zc.currentZoom() > 1.0f);
    REQUIRE(zc.currentZoom() < 1.1f); // Less than a full notch
    REQUIRE(zc.currentZoom() == Approx(std::pow(1.1f, 0.25f)).margin(0.001f));
}

TEST_CASE("Soft bounds decelerate near maximum (AC-2.1.15)", "[ZoomController]")
{
    ZoomController zc;

    // Zoom to near max
    while (zc.currentZoom() < 9.0f)
        zc.applyScrollDelta(120);

    // Record zoom increments near the boundary
    float prev = zc.currentZoom();
    zc.applyScrollDelta(120);
    float delta1 = zc.currentZoom() - prev;

    // Keep going — deltas should get smaller (deceleration)
    prev = zc.currentZoom();
    zc.applyScrollDelta(120);
    float delta2 = zc.currentZoom() - prev;

    // Near the max, each notch should produce diminishing zoom change
    // (soft approach attenuates the scroll delta)
    if (zc.currentZoom() < 10.0f)
    {
        REQUIRE(delta2 <= delta1 + 0.001f); // Allow tiny float imprecision
    }
}

TEST_CASE("Keyboard step sets animating mode", "[ZoomController]")
{
    ZoomController zc;

    zc.applyKeyboardStep(+1);
    REQUIRE(zc.targetZoom() == Approx(1.25f)); // 1.0 + 0.25
    REQUIRE(zc.mode() == ZoomController::Mode::Animating);
}

TEST_CASE("Keyboard step clamps to bounds", "[ZoomController]")
{
    ZoomController zc;

    // Step down from 1.0 — should clamp to 1.0
    zc.applyKeyboardStep(-1);
    REQUIRE(zc.targetZoom() == Approx(1.0f));
}

TEST_CASE("Zoom in then zoom out returns near 1.0x", "[ZoomController]")
{
    ZoomController zc;

    // With logarithmic model, zoom_in(n) then zoom_out(n) should return to start
    // because: zoom * 1.1^n * 1.1^(-n) = zoom
    zc.applyScrollDelta(120);
    zc.applyScrollDelta(120);
    zc.applyScrollDelta(120);
    zc.applyScrollDelta(-120);
    zc.applyScrollDelta(-120);
    zc.applyScrollDelta(-120);

    REQUIRE(zc.currentZoom() == Approx(1.0f).margin(0.01f));
}

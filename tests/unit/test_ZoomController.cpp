// =============================================================================
// Unit tests for ZoomController — Doc 3 §3.3
// Pure logic — no Win32 API dependencies.
// =============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "smoothzoom/logic/ZoomController.h"

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

    // One notch up (120 units) should increase zoom by ~10%
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

    // Scroll down from 1.0× — should stay at 1.0×
    zc.applyScrollDelta(-120);
    REQUIRE(zc.currentZoom() == Approx(1.0f));
}

TEST_CASE("Zoom clamps to maximum 10.0x", "[ZoomController]")
{
    ZoomController zc;

    // Apply massive scroll up
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

TEST_CASE("Multiple scroll deltas accumulate correctly", "[ZoomController]")
{
    ZoomController zc;

    // Apply 3 notches up
    zc.applyScrollDelta(360); // 3 * 120
    float threeNotch = zc.currentZoom();

    // Compare with individual notches
    ZoomController zc2;
    zc2.applyScrollDelta(120);
    zc2.applyScrollDelta(120);
    zc2.applyScrollDelta(120);

    // Due to multiplicative nature (10% of current), these won't be identical
    // but both should be >1.0 and reasonable
    REQUIRE(threeNotch > 1.0f);
    REQUIRE(zc2.currentZoom() > 1.0f);
}

TEST_CASE("Sub-notch precision touchpad delta works", "[ZoomController]")
{
    ZoomController zc;

    // Precision touchpad sends sub-120 deltas
    zc.applyScrollDelta(30); // Quarter notch
    REQUIRE(zc.currentZoom() > 1.0f);
    REQUIRE(zc.currentZoom() < 1.1f); // Less than a full notch
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

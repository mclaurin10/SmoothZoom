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
    // Core property: same ratio requires same number of notches regardless of starting zoom.
    // With pow(1.1, n) model: n = log(ratio) / log(1.1).
    // Test 1→2 vs 2→4 (both 2:1 ratios, both well within soft-bound margins).
    // Note: 5→10 enters the upper soft-bound attenuation zone (above ~7.1×), so we
    // test with 2→4 which stays in the linear region.

    ZoomController zc1;
    // Count notches from 1.0 to 2.0
    int notches_1_to_2 = 0;
    while (zc1.currentZoom() < 2.0f && notches_1_to_2 < 200)
    {
        zc1.applyScrollDelta(120);
        ++notches_1_to_2;
    }

    ZoomController zc2;
    // Get to 2.0× first
    while (zc2.currentZoom() < 2.0f)
        zc2.applyScrollDelta(120);

    // Count notches from ~2.0 to ~4.0 (same 2:1 ratio, away from soft bounds)
    int notches_2_to_4 = 0;
    float start = zc2.currentZoom();
    float target = start * 2.0f;
    while (zc2.currentZoom() < target && notches_2_to_4 < 200)
    {
        zc2.applyScrollDelta(120);
        ++notches_2_to_4;
    }

    // Should be exactly equal (no soft-bound interference in this range)
    REQUIRE(std::abs(notches_1_to_2 - notches_2_to_4) <= 1);
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

TEST_CASE("Zoom in then zoom out returns to same level", "[ZoomController]")
{
    ZoomController zc;

    // Test symmetry in the middle range (away from soft-bound attenuation zones).
    // Near 1.0×, downward scrolling is attenuated (AC-2.1.15), so we first move
    // to ~3× where neither bound's attenuation affects the result.
    for (int i = 0; i < 12; ++i)
        zc.applyScrollDelta(120);
    float baseZoom = zc.currentZoom();
    REQUIRE(baseZoom > 2.5f);

    // 3 notches up, then 3 notches down — should return to baseZoom
    zc.applyScrollDelta(120);
    zc.applyScrollDelta(120);
    zc.applyScrollDelta(120);
    zc.applyScrollDelta(-120);
    zc.applyScrollDelta(-120);
    zc.applyScrollDelta(-120);

    REQUIRE(zc.currentZoom() == Approx(baseZoom).margin(0.001f));
}

// =============================================================================
// Phase 2: Animation tests (AC-2.2.04–AC-2.2.10, AC-2.8.01–AC-2.8.10)
// =============================================================================

TEST_CASE("Keyboard step animates toward target (E2.1)", "[ZoomController][Phase2]")
{
    ZoomController zc;

    zc.applyKeyboardStep(+1);
    REQUIRE(zc.mode() == ZoomController::Mode::Animating);
    REQUIRE(zc.targetZoom() == Approx(1.25f));

    // Simulate several frames at 60fps
    float prevZoom = zc.currentZoom();
    for (int i = 0; i < 5; ++i)
    {
        zc.tick(1.0f / 60.0f);
        REQUIRE(zc.currentZoom() > prevZoom);          // Moving toward target
        REQUIRE(zc.currentZoom() <= zc.targetZoom());   // Not overshooting
        prevZoom = zc.currentZoom();
    }
}

TEST_CASE("Animation completes within reasonable frames (AC-2.2.04)", "[ZoomController][Phase2]")
{
    ZoomController zc;

    zc.applyKeyboardStep(+1); // target = 1.25

    // Run for 60 frames at 60fps (~1s, well beyond 150ms)
    for (int i = 0; i < 60; ++i)
        zc.tick(1.0f / 60.0f);

    REQUIRE(zc.currentZoom() == Approx(1.25f).margin(0.005f));
    REQUIRE(zc.mode() == ZoomController::Mode::Idle);
}

TEST_CASE("Ease-out: velocity decreases each frame (AC-2.2.05)", "[ZoomController][Phase2]")
{
    ZoomController zc;

    zc.applyKeyboardStep(+1); // target = 1.25

    float prev = zc.currentZoom();
    zc.tick(1.0f / 60.0f);
    float delta1 = zc.currentZoom() - prev;

    prev = zc.currentZoom();
    zc.tick(1.0f / 60.0f);
    float delta2 = zc.currentZoom() - prev;

    prev = zc.currentZoom();
    zc.tick(1.0f / 60.0f);
    float delta3 = zc.currentZoom() - prev;

    // Each successive delta should be smaller (decelerating)
    REQUIRE(delta2 < delta1);
    REQUIRE(delta3 < delta2);
}

TEST_CASE("Three rapid keyboard steps retarget smoothly (E2.3)", "[ZoomController][Phase2]")
{
    ZoomController zc;

    // Three rapid presses before any tick (multiplicative: 1.0 * 1.25^3)
    zc.applyKeyboardStep(+1); // target = 1.25
    zc.applyKeyboardStep(+1); // target = 1.5625
    zc.applyKeyboardStep(+1); // target = 1.953125
    float expected = std::pow(1.25f, 3.0f); // ~1.953
    REQUIRE(zc.targetZoom() == Approx(expected));
    REQUIRE(zc.mode() == ZoomController::Mode::Animating);

    // Run to completion
    for (int i = 0; i < 120; ++i)
        zc.tick(1.0f / 60.0f);

    REQUIRE(zc.currentZoom() == Approx(expected).margin(0.005f));
    REQUIRE(zc.mode() == ZoomController::Mode::Idle);
}

TEST_CASE("Plus then minus reverses smoothly (E2.4)", "[ZoomController][Phase2]")
{
    ZoomController zc;

    zc.applyKeyboardStep(+1); // target = 1.25

    // Advance a few frames
    for (int i = 0; i < 3; ++i)
        zc.tick(1.0f / 60.0f);

    float midZoom = zc.currentZoom();
    REQUIRE(midZoom > 1.0f);
    REQUIRE(midZoom < 1.25f);

    // Reverse direction
    zc.applyKeyboardStep(-1); // target = 1.25 - 0.25 = 1.0
    REQUIRE(zc.targetZoom() == Approx(1.0f));
    REQUIRE(zc.mode() == ZoomController::Mode::Animating);

    // Should animate back down
    float prevZoom = zc.currentZoom();
    zc.tick(1.0f / 60.0f);
    REQUIRE(zc.currentZoom() < prevZoom); // Moving back toward 1.0
}

TEST_CASE("Scroll interrupts keyboard animation (E2.5)", "[ZoomController][Phase2]")
{
    ZoomController zc;

    zc.applyKeyboardStep(+1); // target = 1.25, mode = Animating
    REQUIRE(zc.mode() == ZoomController::Mode::Animating);

    // Advance a frame
    zc.tick(1.0f / 60.0f);

    // Scroll arrives — takes over
    zc.applyScrollDelta(120);
    REQUIRE(zc.mode() == ZoomController::Mode::Scrolling);

    // Zoom reflects the scroll, not the animation target
    REQUIRE(zc.currentZoom() == zc.targetZoom()); // Scroll sets both equal
}

TEST_CASE("Keyboard step clamps to max at 9.9x (E2.6)", "[ZoomController][Phase2]")
{
    ZoomController zc;

    // Multiplicative: 1.0 * 1.25^N reaches 10.0 at N = ceil(log(10)/log(1.25)) = 11
    // 12 steps ensures we exceed 10.0 and clamp
    for (int i = 0; i < 12; ++i)
        zc.applyKeyboardStep(+1);

    REQUIRE(zc.targetZoom() == Approx(10.0f));
}

TEST_CASE("No effect when stepping down at 1.0x (E2.2)", "[ZoomController][Phase2]")
{
    ZoomController zc;
    REQUIRE(zc.mode() == ZoomController::Mode::Idle);

    zc.applyKeyboardStep(-1);
    // Should remain idle — no animation started
    REQUIRE(zc.mode() == ZoomController::Mode::Idle);
    REQUIRE(zc.targetZoom() == Approx(1.0f));
}

TEST_CASE("No effect when stepping up at max (AC-2.8.05)", "[ZoomController][Phase2]")
{
    ZoomController zc;

    // Get to max
    for (int i = 0; i < 40; ++i)
        zc.applyKeyboardStep(+1);

    // Run animation to completion
    for (int i = 0; i < 120; ++i)
        zc.tick(1.0f / 60.0f);

    REQUIRE(zc.currentZoom() == Approx(10.0f).margin(0.005f));
    REQUIRE(zc.mode() == ZoomController::Mode::Idle);

    // Try to step up — should be no-op
    zc.applyKeyboardStep(+1);
    REQUIRE(zc.mode() == ZoomController::Mode::Idle);
}

TEST_CASE("animateToZoom(1.0) animates from zoomed state (E2.7)", "[ZoomController][Phase2]")
{
    ZoomController zc;

    // Get to ~2.0x via scroll
    for (int i = 0; i < 8; ++i)
        zc.applyScrollDelta(120);
    REQUIRE(zc.currentZoom() > 1.5f);

    // Animate to 1.0
    zc.animateToZoom(1.0f);
    REQUIRE(zc.mode() == ZoomController::Mode::Animating);
    REQUIRE(zc.targetZoom() == Approx(1.0f));

    // Run to completion
    for (int i = 0; i < 120; ++i)
        zc.tick(1.0f / 60.0f);

    REQUIRE(zc.currentZoom() == Approx(1.0f));
    REQUIRE(zc.mode() == ZoomController::Mode::Idle);
}

TEST_CASE("animateToZoom(1.0) at 1.0x is no-op (E2.8)", "[ZoomController][Phase2]")
{
    ZoomController zc;

    zc.animateToZoom(1.0f);
    REQUIRE(zc.mode() == ZoomController::Mode::Idle); // No animation started
}

TEST_CASE("Animation duration similar at 60Hz and 144Hz", "[ZoomController][Phase2]")
{
    // 60Hz animation
    ZoomController zc60;
    zc60.applyKeyboardStep(+1);
    int frames60 = 0;
    while (zc60.mode() == ZoomController::Mode::Animating && frames60 < 600)
    {
        zc60.tick(1.0f / 60.0f);
        ++frames60;
    }
    float duration60ms = frames60 * (1000.0f / 60.0f);

    // 144Hz animation
    ZoomController zc144;
    zc144.applyKeyboardStep(+1);
    int frames144 = 0;
    while (zc144.mode() == ZoomController::Mode::Animating && frames144 < 1440)
    {
        zc144.tick(1.0f / 144.0f);
        ++frames144;
    }
    float duration144ms = frames144 * (1000.0f / 144.0f);

    // Durations should be within 20% of each other
    float ratio = duration60ms / duration144ms;
    REQUIRE(ratio > 0.8f);
    REQUIRE(ratio < 1.2f);
}

// =============================================================================
// Phase 4: Temporary Toggle tests (AC-2.7.01–AC-2.7.10)
// =============================================================================

// Helper: run animation to completion
static void runToIdle(ZoomController& zc, int maxFrames = 300)
{
    for (int i = 0; i < maxFrames && zc.mode() != ZoomController::Mode::Idle; ++i)
        zc.tick(1.0f / 60.0f);
}

TEST_CASE("Toggle from zoomed → animates to 1.0x (AC-2.7.01)", "[ZoomController][Phase4]")
{
    ZoomController zc;

    // Zoom to ~4.0x
    for (int i = 0; i < 15; ++i)
        zc.applyScrollDelta(120);
    float zoomedLevel = zc.currentZoom();
    REQUIRE(zoomedLevel > 3.0f);

    zc.engageToggle();
    REQUIRE(zc.isToggled());
    REQUIRE(zc.targetZoom() == Approx(1.0f));
    REQUIRE(zc.mode() == ZoomController::Mode::Animating);

    runToIdle(zc);
    REQUIRE(zc.currentZoom() == Approx(1.0f));
}

TEST_CASE("Toggle release → animates back to saved level (AC-2.7.03)", "[ZoomController][Phase4]")
{
    ZoomController zc;

    // Zoom to ~4.0x
    for (int i = 0; i < 15; ++i)
        zc.applyScrollDelta(120);
    float zoomedLevel = zc.currentZoom();

    zc.engageToggle();
    runToIdle(zc);
    REQUIRE(zc.currentZoom() == Approx(1.0f));

    zc.releaseToggle();
    REQUIRE(!zc.isToggled());
    REQUIRE(zc.targetZoom() == Approx(zoomedLevel));
    REQUIRE(zc.mode() == ZoomController::Mode::Animating);

    runToIdle(zc);
    REQUIRE(zc.currentZoom() == Approx(zoomedLevel).margin(0.01f));
}

TEST_CASE("Toggle at 1.0x with prior zoom → animates to lastUsedZoom (AC-2.7.04)", "[ZoomController][Phase4]")
{
    ZoomController zc;

    // Zoom to ~3.0x, then back to 1.0x to set lastUsedZoom
    for (int i = 0; i < 12; ++i)
        zc.applyScrollDelta(120);
    float usedLevel = zc.currentZoom();
    REQUIRE(usedLevel > 2.0f);

    zc.animateToZoom(1.0f);
    runToIdle(zc);
    REQUIRE(zc.currentZoom() == Approx(1.0f));

    // Now toggle from 1.0x — should go to lastUsedZoom
    zc.engageToggle();
    REQUIRE(zc.targetZoom() == Approx(usedLevel).margin(0.01f));

    runToIdle(zc);
    REQUIRE(zc.currentZoom() == Approx(usedLevel).margin(0.01f));
}

TEST_CASE("Toggle at 1.0x first use → animates to 2.0x default (AC-2.7.05)", "[ZoomController][Phase4]")
{
    ZoomController zc;

    // Fresh controller, never zoomed — lastUsedZoom defaults to 2.0
    zc.engageToggle();
    REQUIRE(zc.targetZoom() == Approx(2.0f));

    runToIdle(zc);
    REQUIRE(zc.currentZoom() == Approx(2.0f).margin(0.01f));
}

TEST_CASE("Toggle release from 1.0x toggle → returns to 1.0x (AC-2.7.06)", "[ZoomController][Phase4]")
{
    ZoomController zc;

    // At 1.0x, engage toggle (goes to 2.0x default)
    zc.engageToggle();
    runToIdle(zc);
    REQUIRE(zc.currentZoom() == Approx(2.0f).margin(0.01f));

    // Release → should return to 1.0x (the saved zoom)
    zc.releaseToggle();
    REQUIRE(zc.targetZoom() == Approx(1.0f));

    runToIdle(zc);
    REQUIRE(zc.currentZoom() == Approx(1.0f));
}

TEST_CASE("Brief tap: engage then immediate release → both animations play (AC-2.7.07)", "[ZoomController][Phase4]")
{
    ZoomController zc;

    // Zoom to 4.0x
    for (int i = 0; i < 15; ++i)
        zc.applyScrollDelta(120);
    float zoomedLevel = zc.currentZoom();

    // Engage (targets 1.0x)
    zc.engageToggle();
    REQUIRE(zc.targetZoom() == Approx(1.0f));

    // Immediately release before animation completes
    zc.releaseToggle();
    REQUIRE(zc.targetZoom() == Approx(zoomedLevel));

    // Animation should settle back at the original zoom
    runToIdle(zc);
    REQUIRE(zc.currentZoom() == Approx(zoomedLevel).margin(0.01f));
}

TEST_CASE("Scroll during toggle updates restore target (AC-2.7.09 / E4.5)", "[ZoomController][Phase4]")
{
    ZoomController zc;

    // Start at 1.0x, engage toggle (goes to 2.0x default)
    zc.engageToggle();
    runToIdle(zc);
    REQUIRE(zc.currentZoom() == Approx(2.0f).margin(0.01f));

    // Scroll during toggle to ~3.0x
    for (int i = 0; i < 5; ++i)
        zc.applyScrollDelta(120);
    float scrolledLevel = zc.currentZoom();
    REQUIRE(scrolledLevel > 2.0f);

    // Release — should return to scrolled level, not original 1.0x
    zc.releaseToggle();
    runToIdle(zc);
    REQUIRE(zc.currentZoom() == Approx(scrolledLevel).margin(0.01f));
}

TEST_CASE("Toggle during animation captures mid-animation level (AC-2.7.10 / E4.6)", "[ZoomController][Phase4]")
{
    ZoomController zc;

    // Start keyboard animation (target 1.25x)
    zc.applyKeyboardStep(+1);
    REQUIRE(zc.mode() == ZoomController::Mode::Animating);

    // Advance a few frames to get mid-animation
    for (int i = 0; i < 3; ++i)
        zc.tick(1.0f / 60.0f);

    float midZoom = zc.currentZoom();
    REQUIRE(midZoom > 1.0f);
    REQUIRE(midZoom < 1.25f);

    // Engage toggle mid-animation — captures current (mid) level
    zc.engageToggle();
    REQUIRE(zc.isToggled());
    REQUIRE(zc.targetZoom() == Approx(1.0f)); // Toggling to 1.0x

    // Release should restore to the mid-animation level
    zc.releaseToggle();
    REQUIRE(zc.targetZoom() == Approx(midZoom).margin(0.01f));

    runToIdle(zc);
    REQUIRE(zc.currentZoom() == Approx(midZoom).margin(0.01f));
}

TEST_CASE("Double engage is idempotent", "[ZoomController][Phase4]")
{
    ZoomController zc;

    for (int i = 0; i < 15; ++i)
        zc.applyScrollDelta(120);
    float zoomedLevel = zc.currentZoom();

    zc.engageToggle();
    REQUIRE(zc.isToggled());
    float savedTarget = zc.targetZoom();

    // Second engage — should be no-op
    zc.engageToggle();
    REQUIRE(zc.isToggled());
    REQUIRE(zc.targetZoom() == Approx(savedTarget));
}

TEST_CASE("Release when not toggled is idempotent", "[ZoomController][Phase4]")
{
    ZoomController zc;

    // Should be a safe no-op
    zc.releaseToggle();
    REQUIRE(!zc.isToggled());
    REQUIRE(zc.currentZoom() == Approx(1.0f));
    REQUIRE(zc.mode() == ZoomController::Mode::Idle);
}

// =============================================================================
// Phase 5: Settings application tests (AC-2.9.04–AC-2.9.06)
// =============================================================================

TEST_CASE("applySettings: reduce max while above it → animate down (AC-2.9.05, E5.2)", "[ZoomController][Phase5]")
{
    ZoomController zc;

    // Zoom to ~8.0x
    for (int i = 0; i < 22; ++i)
        zc.applyScrollDelta(120);
    REQUIRE(zc.currentZoom() > 7.0f);

    zc.applySettings(1.0f, 5.0f, 0.25f, 2.0f);
    REQUIRE(zc.targetZoom() == Approx(5.0f));
    REQUIRE(zc.mode() == ZoomController::Mode::Animating);

    runToIdle(zc);
    REQUIRE(zc.currentZoom() == Approx(5.0f).margin(0.005f));
}

TEST_CASE("applySettings: raise min while below it → animate up (AC-2.9.06)", "[ZoomController][Phase5]")
{
    ZoomController zc;
    REQUIRE(zc.currentZoom() == Approx(1.0f));

    zc.applySettings(2.0f, 10.0f, 0.25f, 2.0f);
    REQUIRE(zc.targetZoom() == Approx(2.0f));
    REQUIRE(zc.mode() == ZoomController::Mode::Animating);

    runToIdle(zc);
    REQUIRE(zc.currentZoom() == Approx(2.0f).margin(0.005f));
}

TEST_CASE("applySettings: idle within bounds → no spurious animation", "[ZoomController][Phase5]")
{
    ZoomController zc;

    // Zoom to ~3.0x
    for (int i = 0; i < 12; ++i)
        zc.applyScrollDelta(120);
    float level = zc.currentZoom();
    REQUIRE(level > 2.0f);

    // Change settings but keep bounds that contain current zoom
    zc.applySettings(1.0f, 8.0f, 0.5f, 3.0f);

    // No animation triggered — zoom is within new bounds
    REQUIRE(zc.currentZoom() == Approx(level));
    REQUIRE(zc.mode() == ZoomController::Mode::Scrolling); // Still in scroll mode from delta
}

TEST_CASE("applySettings: new keyboard step takes effect (AC-2.9.04)", "[ZoomController][Phase5]")
{
    ZoomController zc;

    // Apply settings with large step
    zc.applySettings(1.0f, 10.0f, 0.5f, 2.0f);

    zc.applyKeyboardStep(+1);
    // 1.0 * (1 + 0.5) = 1.5 (not 1.25 as with default 0.25 step)
    REQUIRE(zc.targetZoom() == Approx(1.5f));
}

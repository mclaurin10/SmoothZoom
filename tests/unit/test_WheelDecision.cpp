// =============================================================================
// Unit tests — WheelDecision (the WH_MOUSE_LL wheel decision, extracted pure).
// Pins the pre-extraction hook behavior: AC-2.1.01, AC-2.1.02, AC-2.1.16, R-07.
// =============================================================================

#include <catch2/catch_test_macros.hpp>
#include "smoothzoom/input/WheelDecision.h"

using namespace SmoothZoom;

TEST_CASE("Vertical wheel without modifier passes through (AC-2.1.02)", "[WheelDecision]")
{
    for (int vk : {VK_LWIN, VK_LSHIFT, VK_LMENU})
    {
        WheelDecision d = decideWheel(WheelKind::Vertical, 120, vk, false);
        CHECK_FALSE(d.consume);
        CHECK(d.accumulatorDelta == 0);
        CHECK_FALSE(d.markWinUsedForZoom);
    }
}

TEST_CASE("Vertical wheel with modifier is consumed with raw delta (AC-2.1.01)", "[WheelDecision]")
{
    WheelDecision up = decideWheel(WheelKind::Vertical, 120, VK_LWIN, true);
    CHECK(up.consume);
    CHECK(up.accumulatorDelta == 120);

    WheelDecision down = decideWheel(WheelKind::Vertical, -37, VK_LSHIFT, true);
    CHECK(down.consume);
    CHECK(down.accumulatorDelta == -37);
}

TEST_CASE("Start-menu suppression flag only for Win on the vertical path (AC-2.1.16, AC-2.1.20)", "[WheelDecision]")
{
    CHECK(decideWheel(WheelKind::Vertical, 120, VK_LWIN, true).markWinUsedForZoom);
    CHECK(decideWheel(WheelKind::Vertical, 120, VK_RWIN, true).markWinUsedForZoom);
    CHECK_FALSE(decideWheel(WheelKind::Vertical, 120, VK_LSHIFT, true).markWinUsedForZoom);
    CHECK_FALSE(decideWheel(WheelKind::Vertical, 120, VK_LMENU, true).markWinUsedForZoom);
    CHECK_FALSE(decideWheel(WheelKind::Horizontal, 120, VK_LSHIFT, true).markWinUsedForZoom);
}

TEST_CASE("Horizontal wheel is a candidate only when Shift is the modifier (R-07)", "[WheelDecision]")
{
    CHECK_FALSE(wheelIsZoomCandidate(WheelKind::Horizontal, VK_LWIN));
    CHECK_FALSE(wheelIsZoomCandidate(WheelKind::Horizontal, VK_LMENU));
    CHECK(wheelIsZoomCandidate(WheelKind::Horizontal, VK_LSHIFT));
    CHECK(wheelIsZoomCandidate(WheelKind::Horizontal, VK_RSHIFT));
    CHECK(wheelIsZoomCandidate(WheelKind::Horizontal, VK_SHIFT));

    // Win modifier held + HWHEEL: pass through even though "held" is true.
    CHECK_FALSE(decideWheel(WheelKind::Horizontal, 120, VK_LWIN, true).consume);
}

TEST_CASE("Horizontal wheel with Shift modifier is negated (AC-2.1.01)", "[WheelDecision]")
{
    WheelDecision d = decideWheel(WheelKind::Horizontal, 120, VK_LSHIFT, true);
    CHECK(d.consume);
    CHECK(d.accumulatorDelta == -120);
    CHECK(decideWheel(WheelKind::Horizontal, -45, VK_RSHIFT, true).accumulatorDelta == 45);
    CHECK_FALSE(decideWheel(WheelKind::Horizontal, 120, VK_LSHIFT, false).consume);
}

TEST_CASE("int16 negation edge case matches the original hook arithmetic", "[WheelDecision]")
{
    // Original: int16_t delta = ...; delta = -delta;  (-(-32768) wraps to -32768)
    WheelDecision d = decideWheel(WheelKind::Horizontal, INT16_MIN, VK_LSHIFT, true);
    CHECK(d.accumulatorDelta == INT16_MIN);
}

TEST_CASE("modifierHeldFromState maps the configured modifier to its key bit", "[WheelDecision]")
{
    CHECK(modifierHeldFromState(VK_LWIN, false, false, false, true));
    CHECK_FALSE(modifierHeldFromState(VK_LWIN, true, true, true, false));
    CHECK(modifierHeldFromState(VK_RSHIFT, true, false, false, false));
    CHECK(modifierHeldFromState(VK_LMENU, false, false, true, false));
    CHECK(modifierHeldFromState(VK_CONTROL, false, true, false, false));
    CHECK_FALSE(modifierHeldFromState(0x41, true, true, true, true));
}

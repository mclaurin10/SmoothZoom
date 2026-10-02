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

// ── Shift+scroll as the default gesture (AC-2.1.01, AC-2.1.02, AC-2.1.22) ──
// The hook computes "modifier held" from live key state; modifierHeldFromState
// is the same mapping from a modifier snapshot, so these cases run the whole
// decision from "which keys are down" to "consume / pass through".

static WheelDecision wheelWithKeys(int modVK, int16_t delta, bool shift, bool ctrl, bool alt, bool win)
{
    const bool held = wheelIsZoomCandidate(WheelKind::Vertical, modVK)
                   && modifierHeldFromState(modVK, shift, ctrl, alt, win);
    return decideWheel(WheelKind::Vertical, delta, modVK, held);
}

TEST_CASE("Shift modifier: Shift+wheel zooms with the raw delta", "[WheelDecision][ShiftScroll]")
{
    for (int16_t delta : { int16_t(120), int16_t(-120), int16_t(33), int16_t(-7), int16_t(1014) })
    {
        WheelDecision d = wheelWithKeys(VK_LSHIFT, delta, true, false, false, false);
        CHECK(d.consume);
        CHECK(d.accumulatorDelta == delta);
        CHECK_FALSE(d.markWinUsedForZoom);  // no Win-key handling with Shift
    }
}

TEST_CASE("Shift modifier: plain, Ctrl+, Alt+ and Win+wheel pass through", "[WheelDecision][ShiftScroll]")
{
    struct Keys { bool shift, ctrl, alt, win; const char* name; };
    const Keys cases[] = {
        { false, false, false, false, "plain" },
        { false, true,  false, false, "Ctrl" },
        { false, false, true,  false, "Alt" },
        { false, true,  true,  false, "Ctrl+Alt" },
        { false, false, false, true,  "Win" },
    };
    for (const auto& k : cases)
        for (int16_t delta : { int16_t(120), int16_t(-120), int16_t(20) })
        {
            INFO(k.name << " delta=" << delta);
            WheelDecision d = wheelWithKeys(VK_LSHIFT, delta, k.shift, k.ctrl, k.alt, k.win);
            CHECK_FALSE(d.consume);
            CHECK(d.accumulatorDelta == 0);
            CHECK_FALSE(d.markWinUsedForZoom);
        }
}

TEST_CASE("Shift modifier: right Shift and generic VK_SHIFT behave the same", "[WheelDecision][ShiftScroll]")
{
    for (int vk : { VK_RSHIFT, VK_SHIFT })
    {
        CHECK(wheelWithKeys(vk, 120, true, false, false, false).consume);
        CHECK_FALSE(wheelWithKeys(vk, 120, false, true, false, false).consume);
        CHECK_FALSE(wheelWithKeys(vk, 120, false, false, true, false).consume);
    }
}

TEST_CASE("Shift modifier never marks the Win key, on either wheel axis", "[WheelDecision][ShiftScroll]")
{
    for (WheelKind kind : { WheelKind::Vertical, WheelKind::Horizontal })
        for (bool held : { false, true })
            CHECK_FALSE(decideWheel(kind, 120, VK_LSHIFT, held).markWinUsedForZoom);
}

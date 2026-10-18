// =============================================================================
// Unit tests — ScrollGesture (per-gesture mode lock, touchpad coast, device
// attribution, touchpad direction). AC-2.1.25–31, B3.
// =============================================================================

#include <catch2/catch_test_macros.hpp>
#include "smoothzoom/input/ScrollGesture.h"
#include "smoothzoom/input/WheelDecision.h"

#include <vector>

using namespace SmoothZoom;

namespace
{

constexpr int64_t ms(int64_t v) { return v * 1000; }

// One recorded-style event: time, modifier held, touchpad contacts (-1 = no
// contact report at this step).
struct Step
{
    int64_t tUs;
    bool    modifier;
};

struct Counts
{
    int zoom = 0, swallow = 0, pass = 0;
};

Counts tally(const std::vector<ScrollVerdict>& v)
{
    Counts c;
    for (const auto& x : v)
    {
        if (x.action == ScrollAction::Zoom) ++c.zoom;
        else if (x.action == ScrollAction::Swallow) ++c.swallow;
        else ++c.pass;
    }
    return c;
}

// A touchpad flick on the hook path, as Windows delivers it: two fingers down,
// wheel events every 7 ms while they move, the fingers lift, then the coast's
// wheel events keep coming every 7 ms. modifierFrom/modifierTo: the modifier is
// held for events whose index is in [modifierFrom, modifierTo).
struct FlickResult
{
    std::vector<ScrollVerdict> finger, coast;
};

FlickResult touchpadFlick(ScrollGestureTracker& t, bool coast, int64_t t0,
                          int fingerEvents, int coastEvents, int modifierFrom, int modifierTo)
{
    FlickResult r;
    int64_t now = t0;
    t.onTouchpadContacts(2, now);
    int i = 0;
    for (int k = 0; k < fingerEvents; ++k, ++i)
    {
        now += ms(7);
        t.onTouchpadContacts(2, now);   // reports keep arriving while fingers are down
        r.finger.push_back(t.onScroll(i >= modifierFrom && i < modifierTo, coast, now));
    }
    now += ms(3);
    t.onTouchpadContacts(0, now);       // lift
    for (int k = 0; k < coastEvents; ++k, ++i)
    {
        now += ms(7);
        r.coast.push_back(t.onScroll(i >= modifierFrom && i < modifierTo, coast, now));
    }
    return r;
}

} // namespace

// ─── Leak scenario 1: zoom flick, Shift released mid-coast ──────────────────

TEST_CASE("Zoom flick, modifier released mid-coast, coast on: keeps zooming, nothing passes (AC-2.1.26, AC-2.1.28)", "[ScrollGesture]")
{
    ScrollGestureTracker t;
    // Shift held for the finger part and the first 5 coast events, then released.
    FlickResult r = touchpadFlick(t, /*coast*/ true, ms(1000), 10, 30, 0, 15);
    CHECK(tally(r.finger).zoom == 10);
    Counts c = tally(r.coast);
    CHECK(c.zoom == 30);
    CHECK(c.pass == 0);
    for (const auto& v : r.coast)
        CHECK(v.device == ScrollDevice::Touchpad);
}

TEST_CASE("Zoom flick, modifier released mid-coast, coast off: zoom stops at the lift, the coast is swallowed (AC-2.1.29)", "[ScrollGesture]")
{
    ScrollGestureTracker t;
    FlickResult r = touchpadFlick(t, /*coast*/ false, ms(1000), 10, 30, 0, 15);
    CHECK(tally(r.finger).zoom == 10);   // fingers down: zooms
    Counts c = tally(r.coast);
    CHECK(c.swallow == 30);              // after the lift: consumed, not zoomed
    CHECK(c.zoom == 0);
    CHECK(c.pass == 0);                  // and never handed to the app
}

TEST_CASE("Coast off swallows the coast even with the modifier still held", "[ScrollGesture]")
{
    ScrollGestureTracker t;
    FlickResult r = touchpadFlick(t, false, ms(1000), 8, 20, 0, 1000);
    CHECK(tally(r.finger).zoom == 8);
    CHECK(tally(r.coast).swallow == 20);
}

// ─── Leak scenario 2: page-scroll flick, Shift pressed mid-coast ────────────

TEST_CASE("Page-scroll flick with the modifier pressed mid-coast never zooms (AC-2.1.27)", "[ScrollGesture]")
{
    for (bool coast : {true, false})
    {
        ScrollGestureTracker t;
        // No Shift for the finger part; Shift from the 4th coast event on (Shift+click).
        FlickResult r = touchpadFlick(t, coast, ms(1000), 10, 30, 13, 1000);
        CHECK(tally(r.finger).pass == 10);
        Counts c = tally(r.coast);
        CHECK(c.pass == 30);
        CHECK(c.zoom == 0);
        CHECK(c.swallow == 0);
    }
}

TEST_CASE("Modifier pressed mid-gesture with fingers down does not turn a page scroll into a zoom", "[ScrollGesture]")
{
    ScrollGestureTracker t;
    FlickResult r = touchpadFlick(t, true, ms(1000), 20, 0, 5, 1000);
    CHECK(tally(r.finger).pass == 20);
}

// ─── Gesture boundaries ─────────────────────────────────────────────────────

TEST_CASE("Touching the pad stops a zoom coast: its leftovers are swallowed (AC-2.1.28)", "[ScrollGesture]")
{
    // Windows keeps sending the coast for 15-100 ms after the touch (recorded:
    // -171 then -153 -85 -53 -35 after the contact). Those must neither zoom
    // nor reach the app, whatever the modifier and the coast setting.
    for (bool coast : {true, false})
        for (bool shiftNow : {true, false})
        {
            ScrollGestureTracker t;
            touchpadFlick(t, coast, ms(1000), 5, 5, 0, 7);
            int64_t now = ms(1000) + 5 * ms(7) + ms(3) + 5 * ms(7) + ms(10);
            t.onTouchpadContacts(1, now);
            CHECK(t.touchStopped());
            for (int i = 0; i < 5; ++i)
            {
                now += ms(16);
                ScrollVerdict v = t.onScroll(shiftNow, coast, now);
                CHECK(v.action == ScrollAction::Swallow);
                CHECK(v.device == ScrollDevice::Touchpad);
            }
        }
}

TEST_CASE("Touching the pad during a page coast leaves its leftovers to the app, even with Shift (AC-2.1.27)", "[ScrollGesture]")
{
    ScrollGestureTracker t;
    touchpadFlick(t, true, ms(5000), 5, 5, 1000, 1000);
    int64_t now = ms(5000) + 5 * ms(7) + ms(3) + 5 * ms(7) + ms(10);
    t.onTouchpadContacts(2, now);          // e.g. Shift+click: Shift is down now
    for (int i = 0; i < 4; ++i)
    {
        now += ms(16);
        CHECK(t.onScroll(true, true, now).action == ScrollAction::Pass);
    }
}

TEST_CASE("A new stroke after the touch starts a new gesture with the modifier then (AC-2.1.25)", "[ScrollGesture]")
{
    // Zoom coast, Shift released, touch, leftovers, then a page-scroll stroke.
    ScrollGestureTracker t;
    touchpadFlick(t, true, ms(1000), 5, 5, 0, 7);
    int64_t now = ms(1000) + 5 * ms(7) + ms(3) + 5 * ms(7) + ms(10);
    t.onTouchpadContacts(2, now);
    now += ms(16);
    CHECK(t.onScroll(false, true, now).action == ScrollAction::Swallow);
    now += ms(20);
    t.onTouchpadStroke(now);
    CHECK_FALSE(t.touchStopped());
    now += ms(7);
    CHECK(t.onScroll(false, true, now).action == ScrollAction::Pass);

    // Page coast, touch, new stroke with Shift held: a zoom gesture.
    ScrollGestureTracker u;
    touchpadFlick(u, true, ms(5000), 5, 5, 1000, 1000);
    now = ms(5000) + 5 * ms(7) + ms(3) + 5 * ms(7) + ms(10);
    u.onTouchpadContacts(2, now);
    now += ms(16);
    CHECK(u.onScroll(true, true, now).action == ScrollAction::Pass);   // leftover
    u.onTouchpadStroke(now + ms(5));
    CHECK(u.onScroll(true, true, now + ms(12)).action == ScrollAction::Zoom);
}

TEST_CASE("A touch-stopped coast also ends on a pause", "[ScrollGesture]")
{
    ScrollGestureTracker t;
    touchpadFlick(t, true, ms(1000), 5, 5, 0, 7);
    int64_t now = ms(1000) + 5 * ms(7) + ms(3) + 5 * ms(7) + ms(10);
    t.onTouchpadContacts(1, now);
    now += ms(16);
    CHECK(t.onScroll(false, true, now).action == ScrollAction::Swallow);
    now += kGesturePauseUs + ms(1);
    CHECK(t.onScroll(false, true, now).action == ScrollAction::Pass);
    CHECK_FALSE(t.touchStopped());
}

TEST_CASE("A new contact with no coast running ends the gesture at once (AC-2.1.25)", "[ScrollGesture]")
{
    // The previous gesture paused out before the touch: no stop window.
    ScrollGestureTracker t;
    touchpadFlick(t, true, ms(1000), 5, 5, 0, 1000);
    int64_t now = ms(1000) + 5 * ms(7) + ms(3) + 5 * ms(7) + kGesturePauseUs + ms(5);
    t.onTouchpadContacts(2, now);
    CHECK_FALSE(t.touchStopped());
    CHECK(t.onScroll(false, true, now + ms(7)).action == ScrollAction::Pass);

    // A mouse gesture is never "coasting": a touch ends it at once.
    ScrollGestureTracker m;
    CHECK(m.onScroll(true, true, ms(100)).device == ScrollDevice::Mouse);
    m.onTouchpadContacts(2, ms(120));
    CHECK_FALSE(m.touchStopped());
    CHECK(m.onScroll(false, true, ms(130)).action == ScrollAction::Pass);
}

TEST_CASE("A stroke signal without a stopped coast changes nothing", "[ScrollGesture]")
{
    ScrollGestureTracker t;
    t.onTouchpadContacts(2, ms(100));
    CHECK(t.onScroll(true, true, ms(107)).action == ScrollAction::Zoom);
    t.onTouchpadStroke(ms(110));
    CHECK(t.onScroll(false, true, ms(114)).action == ScrollAction::Zoom);
}

TEST_CASE("A finger already down is not a new contact; a second finger landing is not either", "[ScrollGesture]")
{
    ScrollGestureTracker t;
    t.onTouchpadContacts(1, ms(100));
    CHECK(t.onScroll(true, true, ms(110)).action == ScrollAction::Zoom);
    t.onTouchpadContacts(2, ms(115));   // 1 -> 2: same touch
    CHECK(t.onScroll(false, true, ms(120)).action == ScrollAction::Zoom);
}

TEST_CASE("A pause longer than the threshold ends a gesture (AC-2.1.25)", "[ScrollGesture]")
{
    ScrollGestureTracker t;
    CHECK(t.onScroll(true, true, ms(1000)).action == ScrollAction::Zoom);
    CHECK(t.onScroll(false, true, ms(1000) + kGesturePauseUs).action == ScrollAction::Zoom);   // exactly the threshold: same gesture
    CHECK(t.inGesture(ms(1000) + 2 * kGesturePauseUs));
    CHECK_FALSE(t.inGesture(ms(1000) + 2 * kGesturePauseUs + 1));
    // One microsecond past the threshold: a new gesture, decided by the modifier now.
    CHECK(t.onScroll(false, true, ms(1000) + 2 * kGesturePauseUs + 1).action == ScrollAction::Pass);
}

TEST_CASE("The 39-80 ms in-gesture pauses seen in the recordings do not split a gesture", "[ScrollGesture]")
{
    STATIC_REQUIRE(kGesturePauseUs > ms(80));
    for (int64_t gap : {ms(39), ms(55), ms(62), ms(75), ms(80)})
    {
        ScrollGestureTracker t;
        int64_t now = ms(1000);
        CHECK(t.onScroll(true, true, now).action == ScrollAction::Zoom);
        now += gap;
        CHECK(t.onScroll(false, true, now).action == ScrollAction::Zoom);   // still the zoom gesture
        now += ms(7);
        CHECK(t.onScroll(false, true, now).action == ScrollAction::Zoom);
    }
}

TEST_CASE("Fingers resting still longer than the pause, then moving: a new gesture", "[ScrollGesture]")
{
    ScrollGestureTracker t;
    int64_t now = ms(1000);
    t.onTouchpadContacts(2, now);
    CHECK(t.onScroll(true, true, now + ms(7)).action == ScrollAction::Zoom);
    for (int i = 1; i <= 40; ++i)
        t.onTouchpadContacts(2, now + ms(7) + i * ms(8));   // still down, not moving
    CHECK(t.onScroll(false, true, now + ms(7) + ms(320) + ms(7)).action == ScrollAction::Pass);
}

// ─── Mouse wheel ────────────────────────────────────────────────────────────

TEST_CASE("Mouse wheel: the mode lock applies and the coast setting is ignored (AC-2.1.30)", "[ScrollGesture]")
{
    for (bool coast : {true, false})
    {
        ScrollGestureTracker t;
        int64_t now = ms(2000);
        // Shift+spin, Shift released after 4 notches, the wheel keeps turning 45 ms apart.
        for (int i = 0; i < 12; ++i, now += ms(45))
        {
            ScrollVerdict v = t.onScroll(i < 4, coast, now);
            CHECK(v.action == ScrollAction::Zoom);
            CHECK(v.device == ScrollDevice::Mouse);
        }
        // After a pause the wheel scrolls the page again.
        now += ms(200);
        CHECK(t.onScroll(false, coast, now).action == ScrollAction::Pass);

        // A page scroll does not turn into a zoom when Shift goes down mid-spin.
        ScrollGestureTracker u;
        now = ms(9000);
        for (int i = 0; i < 12; ++i, now += ms(40))
            CHECK(u.onScroll(i >= 3, coast, now).action == ScrollAction::Pass);
    }
}

TEST_CASE("Mouse wheel during a touchpad coast joins the coasting gesture (chosen behavior)", "[ScrollGesture]")
{
    // The hook cannot tell a wheel notch from a coast event, so a notch that
    // arrives while a touchpad gesture is coasting is part of that gesture:
    // its mode, the touchpad's settings, and with coast off it is swallowed.
    ScrollGestureTracker t;
    FlickResult r = touchpadFlick(t, true, ms(1000), 5, 5, 0, 1000);
    int64_t now = ms(1000) + 5 * ms(7) + ms(3) + 5 * ms(7) + ms(20);
    ScrollVerdict v = t.onScroll(false, true, now);
    CHECK(v.device == ScrollDevice::Touchpad);
    CHECK(v.action == ScrollAction::Zoom);

    ScrollGestureTracker off;
    touchpadFlick(off, false, ms(1000), 5, 5, 0, 1000);
    CHECK(off.onScroll(true, false, now).action == ScrollAction::Swallow);

    // Once the coast has ended (a pause), the wheel is a mouse gesture again.
    now += kGesturePauseUs + ms(1);
    ScrollVerdict w = t.onScroll(false, true, now);
    CHECK(w.device == ScrollDevice::Mouse);
    CHECK(w.action == ScrollAction::Pass);
}

// ─── Attribution ────────────────────────────────────────────────────────────

TEST_CASE("Attribution: contact down = touchpad, none = mouse (AC-2.1.30)", "[ScrollGesture]")
{
    ScrollGestureTracker t;
    CHECK(t.onScroll(true, true, ms(100)).device == ScrollDevice::Mouse);

    ScrollGestureTracker u;
    u.onTouchpadContacts(1, ms(100));   // even one finger resting
    CHECK(u.onScroll(true, true, ms(105)).device == ScrollDevice::Touchpad);
}

TEST_CASE("A flick whose first wheel event comes after a two-finger lift is still the touchpad", "[ScrollGesture]")
{
    ScrollGestureTracker t;
    t.onTouchpadContacts(2, ms(100));
    t.onTouchpadContacts(0, ms(140));
    ScrollVerdict v = t.onScroll(true, true, ms(150));
    CHECK(v.device == ScrollDevice::Touchpad);
    CHECK(v.action == ScrollAction::Zoom);
    // With coast off that whole gesture is coast: swallowed.
    ScrollGestureTracker u;
    u.onTouchpadContacts(2, ms(100));
    u.onTouchpadContacts(0, ms(140));
    CHECK(u.onScroll(true, false, ms(150)).action == ScrollAction::Swallow);
}

TEST_CASE("A one-finger touch (pointer movement) does not claim the next wheel event", "[ScrollGesture]")
{
    ScrollGestureTracker t;
    t.onTouchpadContacts(1, ms(100));
    t.onTouchpadContacts(0, ms(300));
    ScrollVerdict v = t.onScroll(true, false, ms(320));
    CHECK(v.device == ScrollDevice::Mouse);
    CHECK(v.action == ScrollAction::Zoom);
}

TEST_CASE("Stale contact reports count as lifted (a lost final report)", "[ScrollGesture]")
{
    ScrollGestureTracker t;
    t.onTouchpadContacts(2, ms(100));
    CHECK(t.touchpadDown(ms(100) + kContactStaleUs));
    CHECK_FALSE(t.touchpadDown(ms(100) + kContactStaleUs + 1));
    CHECK(t.onScroll(true, true, ms(2000)).device == ScrollDevice::Mouse);
    // The next real touch after the stale gap is a new contact.
    CHECK(t.onScroll(true, true, ms(2050)).action == ScrollAction::Zoom);
    t.onTouchpadContacts(2, ms(2060));
    CHECK(t.onScroll(false, true, ms(2067)).action == ScrollAction::Pass);
}

TEST_CASE("Touchpad without contact reports: pause-based gestures, coast setting has no effect", "[ScrollGesture]")
{
    // Same event stream as a flick, but no contact reports arrive at all.
    ScrollGestureTracker t;
    int64_t now = ms(1000);
    for (int i = 0; i < 30; ++i, now += ms(7))
    {
        ScrollVerdict v = t.onScroll(i < 10, /*coast*/ false, now);
        CHECK(v.device == ScrollDevice::Mouse);
        CHECK(v.action == ScrollAction::Zoom);   // never swallowed
    }
}

TEST_CASE("PTP-path finger motion is touchpad and never a coast", "[ScrollGesture]")
{
    ScrollGestureTracker t;
    // No contact report (older trace), coast off: still zooms.
    ScrollVerdict v = t.onScroll(true, false, ms(100), /*fromTouchpad*/ true);
    CHECK(v.device == ScrollDevice::Touchpad);
    CHECK(v.action == ScrollAction::Zoom);
    // The same gesture continued by hook events: locked to zoom; with no
    // fingers reported down they are its coast.
    CHECK(t.onScroll(false, true, ms(107)).action == ScrollAction::Zoom);
    CHECK(t.onScroll(false, false, ms(114)).action == ScrollAction::Swallow);
}

TEST_CASE("PTP path then hook path inside one swipe keep one mode", "[ScrollGesture]")
{
    // Edge-to-Terminal style handover seen in shift-touchpad-flick: PTP rows
    // first, then the hook takes over. Shift released in between.
    ScrollGestureTracker t;
    t.onTouchpadContacts(2, ms(100));
    CHECK(t.onScroll(true, true, ms(105), true).action == ScrollAction::Zoom);
    t.onTouchpadContacts(2, ms(110));
    CHECK(t.onScroll(false, true, ms(112)).action == ScrollAction::Zoom);
}

TEST_CASE("reset() forgets the gesture and the contacts", "[ScrollGesture]")
{
    ScrollGestureTracker t;
    t.onTouchpadContacts(2, ms(100));
    CHECK(t.onScroll(true, true, ms(105)).action == ScrollAction::Zoom);
    t.reset();
    CHECK_FALSE(t.touchpadDown(ms(106)));
    CHECK(t.onScroll(false, true, ms(107)).action == ScrollAction::Pass);
}

// ─── Wheel decision inside a gesture ────────────────────────────────────────

TEST_CASE("decideWheelInGesture: Pass/Zoom match decideWheel, Swallow consumes with no delta", "[ScrollGesture][WheelDecision]")
{
    for (int vk : {VK_LSHIFT, VK_LWIN, VK_LMENU})
    {
        WheelDecision pass = decideWheelInGesture(WheelKind::Vertical, 120, vk, ScrollAction::Pass);
        WheelDecision ref0 = decideWheel(WheelKind::Vertical, 120, vk, false);
        CHECK(pass.consume == ref0.consume);
        CHECK(pass.accumulatorDelta == ref0.accumulatorDelta);

        WheelDecision zoom = decideWheelInGesture(WheelKind::Vertical, -37, vk, ScrollAction::Zoom);
        WheelDecision ref1 = decideWheel(WheelKind::Vertical, -37, vk, true);
        CHECK(zoom.consume == ref1.consume);
        CHECK(zoom.accumulatorDelta == ref1.accumulatorDelta);
        CHECK(zoom.markWinUsedForZoom == ref1.markWinUsedForZoom);

        WheelDecision sw = decideWheelInGesture(WheelKind::Vertical, -37, vk, ScrollAction::Swallow);
        CHECK(sw.consume);
        CHECK(sw.accumulatorDelta == 0);
    }
    // Horizontal stays a candidate only with Shift (R-27); its sign rule is kept.
    CHECK(decideWheelInGesture(WheelKind::Horizontal, 120, VK_LSHIFT, ScrollAction::Zoom).accumulatorDelta == -120);
    CHECK_FALSE(decideWheelInGesture(WheelKind::Horizontal, 120, VK_LWIN, ScrollAction::Zoom).consume);
}

// ─── Touchpad direction (AC-2.1.31) ─────────────────────────────────────────

TEST_CASE("Fingers up zoom in on both paths under either Windows scrolling direction", "[ScrollGesture][Direction]")
{
    const float fingersUp = -40.0f;   // PTP Y decreases when the fingers move up
    for (bool natural : {false, true})
    {
        // PTP path: finger motion -> hook convention -> touchpad direction.
        float ptpHook = ptpToHookConvention(fingersUp, natural);
        int32_t ptpCanon = touchpadCanonicalDelta(static_cast<int32_t>(ptpHook), natural);
        CHECK(ptpCanon > 0);

        // Hook path: Windows delivers fingers-up as a positive wheel delta with
        // traditional scrolling and a negative one with natural scrolling.
        int32_t hookDelta = natural ? -40 : 40;
        CHECK(touchpadCanonicalDelta(hookDelta, natural) > 0);
        // Same sign on both paths: the same motion zooms the same way in
        // Edge (PTP path) and in Notepad/Terminal (hook path).
        CHECK((ptpCanon > 0) == (touchpadCanonicalDelta(hookDelta, natural) > 0));
    }
}

TEST_CASE("PTP hook-convention sign matches the hook (recorded handover, natural on)", "[ScrollGesture][Direction]")
{
    // shift-touchpad-flick.csv (TEMPLAPTOP, natural scrolling on): the PTP rows
    // -1 -4 -10 ... -21 hand over to hook rows -35 -88 ... in one swipe. The PTP
    // conversion must give the hook's sign for the same motion.
    CHECK(ptpToHookConvention(-21.0f, true) < 0);
    CHECK(ptpToHookConvention(-21.0f, false) > 0);
}

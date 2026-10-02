#pragma once
// =============================================================================
// SmoothZoom — ScrollGesture
// Per-gesture mode lock for scroll-gesture zoom (AC-2.1.25–29, B3) and the
// touchpad/mouse attribution of each scroll event (AC-2.1.30).
//
// Before this, every scroll event was tested for the modifier on its own, so
//   - the coast (inertia) of a Shift+flick scrolled the page once Shift was
//     released, and
//   - a page-scroll flick turned into a zoom if Shift went down while it was
//     still coasting (e.g. to Shift+click).
// Here a gesture's mode (zoom or pass-through) is decided by the modifier at
// its first event and holds until the gesture ends.
//
// A gesture starts on the first scroll event after
//   - a new touchpad contact (no finger down -> a finger down), or
//   - a pause longer than kGesturePauseUs since the previous scroll event.
// Touching the touchpad while a touchpad gesture is coasting stops the coast:
// Windows keeps sending the coast's wheel events for another 15-100 ms after
// the touch (recorded on TEMPLAPTOP, bench/traces-recorded-gestures/), so those
// leftovers stay in the old gesture - swallowed if it was a zoom, still passed
// on if it was a page scroll - until the fingers start a new stroke (the PTP
// path reports kTouchpadStrokeStartUnits of two-finger travel) or a pause.
// A zoom gesture on the touchpad keeps zooming through the coast after the
// fingers lift when the coast setting (momentumZoom) is on; when it is off the
// rest of the coast is swallowed (consumed, never zoomed, never passed on).
// A mouse wheel has no lift, so the coast setting never applies to it.
//
// Attribution: an event is a touchpad event while a contact is down, while a
// touchpad gesture is coasting, or when a two-finger touch lifted less than a
// pause ago (a flick whose first wheel event arrives after the lift). Otherwise
// it is a mouse event. Delta size is deliberately not used (high-resolution
// wheels send small deltas too). Touchpads without Precision Touchpad contact
// reports never report contacts, so their events are mouse events: gestures
// end only on a pause and the coast setting has no effect.
//
// Pure logic, no Win32 (unit-tested, used by --bench replay). Hook-safe: a few
// comparisons on member state, no allocation, no I/O (R-05). Single-threaded:
// the hook, the Raw Input / PTP handlers and the replay all run on the main
// thread.
// =============================================================================

#include "smoothzoom/common/Types.h"
#include <cstdint>

namespace SmoothZoom
{

// Pause that ends a gesture. Derived from the recorded traces
// (bench/traces/recorded/, bench/hardware/wheel-observe.csv, TEMPLAPTOP): the
// longest pause inside one gesture is 75 ms (a slow two-finger drag; coast
// tails pause 39-62 ms), and separate gestures were at least 144 ms apart
// (touchpad) / ~150-450 ms apart (wheel re-grips). 150 ms is twice the longest
// in-gesture pause, so one swipe or coast is never split in two.
inline constexpr int64_t kGesturePauseUs = 150000;

// A Precision Touchpad reports at >= 125 Hz while a finger is down. If no
// report arrives for this long, the contacts are treated as lifted (a lost
// final report must not leave every later wheel event on the touchpad).
inline constexpr int64_t kContactStaleUs = 250000;

// Two-finger travel since the touch (wheel-equivalent units, 120 = one notch,
// about 8% of the pad's height) that counts as a new stroke: a quarter notch.
// Touch-down jitter stays well below it.
inline constexpr float kTouchpadStrokeStartUnits = 30.0f;

enum class ScrollAction : uint8_t
{
    Pass,     // not ours: the application gets the event
    Zoom,     // consume and zoom
    Swallow,  // consume, but do not zoom (coast of a zoom gesture, coast off)
};

struct ScrollVerdict
{
    ScrollAction action = ScrollAction::Pass;
    ScrollDevice device = ScrollDevice::Mouse;
};

class ScrollGestureTracker
{
public:
    // Touchpad contact state from a Precision Touchpad report: the number of
    // fingers touching (0 = all lifted). Called for every report.
    void onTouchpadContacts(int contacts, int64_t nowUs)
    {
        const bool wasDown = touchpadDown(nowUs);
        const bool down = contacts > 0;
        if (down && !wasDown)
        {
            // New contact. During a touchpad coast it stops the coast: the
            // coast's leftover events keep the old gesture (a zoom is
            // swallowed from now on) until a new stroke or a pause. Otherwise
            // it ends the gesture: the next scroll event starts a new one with
            // the modifier state at that moment.
            if (mode_ != Mode::None && device_ == ScrollDevice::Touchpad
                && nowUs - lastScrollUs_ <= kGesturePauseUs)
                touchStopped_ = true;
            else
                endGesture();
            peakContacts_ = 0;
        }
        else if (!down && wasDown && peakContacts_ >= 2)
        {
            liftUs_ = nowUs;
            haveLift_ = true;
        }
        if (down && contacts > peakContacts_)
            peakContacts_ = contacts;
        contacts_ = contacts;
        lastContactReportUs_ = nowUs;
        haveContactReport_ = true;
    }

    // The fingers have started a new stroke (two-finger travel past
    // kTouchpadStrokeStartUnits since the touch). Ends a touch-stopped coast,
    // so the stroke's own events start a new gesture.
    void onTouchpadStroke(int64_t /*nowUs*/)
    {
        if (touchStopped_)
            endGesture();
    }

    // Decide one scroll event. modifierHeld: the live modifier test (only used
    // when this event starts a gesture). coastEnabled: the touchpad coast
    // setting. fromTouchpad: the event came from the touchpad's own reports
    // (PTP path), so it is a touchpad event whatever the contact state.
    ScrollVerdict onScroll(bool modifierHeld, bool coastEnabled, int64_t nowUs,
                           bool fromTouchpad = false)
    {
        if (mode_ != Mode::None && nowUs - lastScrollUs_ > kGesturePauseUs)
            endGesture();   // paused: the gesture is over

        const bool down = touchpadDown(nowUs);
        if (mode_ == Mode::None)
        {
            const bool recentLift = haveLift_ && nowUs - liftUs_ <= kGesturePauseUs;
            device_ = (fromTouchpad || down || recentLift) ? ScrollDevice::Touchpad
                                                           : ScrollDevice::Mouse;
            mode_ = modifierHeld ? Mode::Zoom : Mode::Pass;
        }
        lastScrollUs_ = nowUs;

        ScrollVerdict v;
        v.device = device_;
        if (mode_ == Mode::Pass)
            v.action = ScrollAction::Pass;
        else if (touchStopped_)
            v.action = ScrollAction::Swallow;   // the touch stopped this zoom coast
        else if (device_ == ScrollDevice::Touchpad && !down && !fromTouchpad && !coastEnabled)
            v.action = ScrollAction::Swallow;   // fingers lifted: the coast is not zoomed
        else
            v.action = ScrollAction::Zoom;
        return v;
    }

    // A finger is on the touchpad (per a recent contact report).
    bool touchpadDown(int64_t nowUs) const
    {
        return haveContactReport_ && contacts_ > 0
            && nowUs - lastContactReportUs_ <= kContactStaleUs;
    }

    // True after a touch stopped the current coast (until a stroke or pause).
    bool touchStopped() const { return touchStopped_; }

    // True while a gesture is in progress (no pause yet since its last event).
    bool inGesture(int64_t nowUs) const
    {
        return mode_ != Mode::None && nowUs - lastScrollUs_ <= kGesturePauseUs;
    }

    // Forget everything (session lock/unlock, resume: input was missed).
    void reset() { *this = ScrollGestureTracker{}; }

private:
    enum class Mode : uint8_t { None, Zoom, Pass };

    void endGesture()
    {
        mode_ = Mode::None;
        touchStopped_ = false;
    }

    Mode         mode_ = Mode::None;
    bool         touchStopped_ = false;    // a touch stopped this gesture's coast
    ScrollDevice device_ = ScrollDevice::Mouse;
    int64_t      lastScrollUs_ = 0;

    int          contacts_ = 0;
    int          peakContacts_ = 0;        // most fingers down during the current touch
    int64_t      lastContactReportUs_ = 0;
    bool         haveContactReport_ = false;
    int64_t      liftUs_ = 0;              // when the last two-finger touch lifted
    bool         haveLift_ = false;
};

// Touchpad direction (AC-2.1.31): one sign for a touchpad gesture whatever
// Windows' touchpad scrolling direction: fingers moving up (away from the user)
// is positive, i.e. zoom in, like rolling a wheel away. Windows flips the wheel
// messages it synthesizes for the touchpad when natural ("down motion scrolls
// up") scrolling is on, so that flip is undone here. Takes a delta in the
// convention the WH_MOUSE_LL hook sees (the PTP path converts to it first).
inline int32_t touchpadCanonicalDelta(int32_t hookConventionDelta, bool naturalScrolling)
{
    return naturalScrolling ? -hookConventionDelta : hookConventionDelta;
}

// PTP finger Y motion (device units, +Y = fingers moving down) in the
// convention the hook would see for the same motion under the given Windows
// setting. Traditional: fingers down scroll down (negative wheel delta).
inline float ptpToHookConvention(float fingerDeltaY, bool naturalScrolling)
{
    return naturalScrolling ? fingerDeltaY : -fingerDeltaY;
}

} // namespace SmoothZoom

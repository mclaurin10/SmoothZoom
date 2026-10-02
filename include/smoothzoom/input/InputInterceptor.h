#pragma once
// =============================================================================
// SmoothZoom — InputInterceptor
// Installs WH_MOUSE_LL and WH_KEYBOARD_LL hooks, routes events.
// Doc 3 §3.1
// =============================================================================

#include "smoothzoom/input/ScrollGesture.h"
#include <cstdint>

#ifdef SMOOTHZOOM_DEV_TOOLS
#include "smoothzoom/common/DevTelemetry.h"
#include "smoothzoom/input/WheelDecision.h"
#endif

namespace SmoothZoom
{

struct SharedState;
class SettingsManager;

class InputInterceptor
{
public:
    bool install(SharedState& state);
    void uninstall();
    bool isHealthy() const;

    // Reinstall deregistered hooks (R-05 watchdog, AC-ERR.03)
    bool reinstall();

    // Unconditional unhook + rehook for the R-05 liveness watchdog. Silent OS
    // deregistration leaves the HHOOK non-null, so isHealthy()/reinstall()
    // cannot recover from it. Also resets transient key state (key-ups were
    // likely missed during the outage).
    bool forceReinstall();

    // Crash handler: put the Sticky Keys shortcut back if SmoothZoom suppressed
    // it (AC-2.1.24). No allocation, no registry, no logging.
    static bool crashRestoreStickyKeys();

    // GetTickCount64 timestamp of the most recent hook callback invocation.
    // The watchdog compares this against GetLastInputInfo to detect dead hooks.
    static int64_t lastCallbackTick();

    // Clear all held/engaged key state derived from hook events (Win key state
    // machine, modifier/toggle flags). Call when key-ups may have been missed:
    // secure-desktop lock/unlock (AC-ERR.04) and hook reinstall.
    static void resetTransientKeyState();

    // Phase 5B: Register for settings change notifications (AC-2.9.04).
    // Callback updates configurable modifier/toggle keys on main thread.
    static void registerSettingsObserver(SettingsManager& mgr);

    // Phase 5B: Store message window handle for Win+Ctrl+M posting (AC-2.8.11).
    // Uses void* to avoid pulling in windows.h in the header.
    static void setMessageWindow(void* hWnd);

    // ── Raw Input / PTP scroll paths (main thread, main.cpp WM_INPUT) ──
    // They share the hook's gesture tracker (ScrollGesture.h), so one physical
    // gesture keeps the mode its first event set on every path (AC-2.1.25–29).

    // Precision Touchpad report: number of fingers touching (0 = lifted).
    // Called for every report; a new contact ends the current gesture.
    static void onTouchpadContacts(int contacts);

    // The fingers started a new stroke after a touch (two-finger travel past
    // kTouchpadStrokeStartUnits). Ends a coast the touch stopped (AC-2.1.28).
    static void onTouchpadStroke();

    // PTP two-finger scroll, in the hook's wheel convention (120 per notch).
    // Zooms (touchpad accumulator) only if the gesture is a zoom gesture.
    static ScrollVerdict onTouchpadScroll(int32_t hookConventionDelta, bool modifierHeld);

    // Raw Input mouse-wheel fallback (the hook did not see the event).
    static ScrollVerdict onRawWheel(int16_t delta, bool modifierHeld);

    // Windows' touchpad scrolling direction (true = natural, "down motion
    // scrolls up"). Used to give touchpad gestures one direction (AC-2.1.31).
    static void setTouchpadNaturalScrolling(bool natural);

#ifdef SMOOTHZOOM_DEV_TOOLS
    // --record: observe every wheel message the mouse hook sees. The tap runs
    // inside the hook callback (main thread), so it must only copy the record
    // into a pre-allocated buffer. nullptr disables it.
    static void setWheelTap(WheelTapFn fn);

    // --bench: feed one wheel event through the hook's own decision (gesture
    // tracker + decideWheelInGesture) and side effects, from the main thread,
    // with the modifier verdict supplied by the trace. The hook consumes it
    // unless the action is Pass.
    static ScrollVerdict devInjectWheel(WheelKind kind, int16_t rawDelta, bool modifierHeld);

    // Currently configured scroll-gesture modifier (main thread).
    static int configuredModifierVK();

    // Windows touchpad scrolling direction in use (for the --record header).
    static bool touchpadNaturalScrolling();
#endif
};

} // namespace SmoothZoom

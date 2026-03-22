# SmoothZoom — Fix Shift+Scroll Reversed Zoom Direction

You are fixing a bug in **SmoothZoom**, a native C++17 Win32 desktop application that provides macOS-style full-screen magnification using the Magnification API. Five design documents are uploaded to this project — reference them for architectural context.

---

## Bug Description

When the user configures **Shift** as the scroll-gesture modifier key, the zoom direction is reversed: scrolling up (away from user) zooms *out* instead of *in*, and scrolling down zooms *in* instead of *out*. This does not occur with the default Win modifier or with Ctrl/Alt.

The bug is also reproducible on the desktop specifically (not over all windows), suggesting the Windows shell may be involved in the conversion behavior.

---

## Root Cause Analysis

This is almost certainly caused by **Windows converting Shift+Scroll into horizontal scroll events**. This is a well-known OS-level behavior documented in risk R-07 of Doc 5 ("Shift+Scroll: Used by some applications for horizontal scrolling").

The likely chain of events:

1. User holds Shift and scrolls up (positive vertical delta)
2. Windows (or the mouse driver) converts the `WM_MOUSEWHEEL` into `WM_MOUSEHWHEEL` *before* it reaches the low-level hook — or the low-level hook receives `WM_MOUSEWHEEL` but the delta has been modified
3. `WM_MOUSEHWHEEL` uses **opposite sign convention** from `WM_MOUSEWHEEL`: positive delta means scroll *right*, not scroll *up*. Physically scrolling "up" may arrive as a negative horizontal delta
4. InputInterceptor's mouse hook currently processes `WM_MOUSEWHEEL` for zoom but may not handle `WM_MOUSEHWHEEL` correctly, or may be receiving the converted event with inverted delta

---

## Investigation Steps (Do These First)

Before writing any fix, add temporary diagnostic logging to the mouse hook callback in `InputInterceptor.cpp` to confirm what's actually arriving. Log:

1. **The `wParam` message ID** — is it `WM_MOUSEWHEEL` (0x020A) or `WM_MOUSEHWHEEL` (0x020E)?
2. **The raw delta value** from `HIWORD(mouseData)` — what sign and magnitude?
3. **Which modifier key is held** at the time

Test with each modifier (Win, Ctrl, Alt, Shift) and log what the hook receives for "scroll up" and "scroll down" physical gestures. This will reveal exactly where the conversion happens.

The three possible scenarios:

- **Scenario A:** With Shift held, the hook receives `WM_MOUSEHWHEEL` instead of `WM_MOUSEWHEEL`, with inverted delta sign convention. This means Windows converts the event *before* the low-level hook.
- **Scenario B:** With Shift held, the hook still receives `WM_MOUSEWHEEL` but with a negated delta. This would mean the driver or OS is modifying the delta in place.
- **Scenario C:** The hook receives `WM_MOUSEWHEEL` with correct delta, but something downstream is inverting it. Less likely given the symptom.

---

## Fix Implementation

Based on the investigation results, implement the appropriate fix. The fix lives entirely in `InputInterceptor.cpp`'s mouse hook callback — no other component should need changes.

### If Scenario A (most likely): Hook receives `WM_MOUSEHWHEEL` when Shift is modifier

The hook callback's scroll processing currently triggers on `WM_MOUSEWHEEL`. When Shift is the configured modifier, it must **also** handle `WM_MOUSEHWHEEL`:

```
If event is WM_MOUSEWHEEL and modifier is held:
    delta = (short)HIWORD(mouseData)    // standard vertical delta
    accumulate(delta)
    consume event

If event is WM_MOUSEHWHEEL and modifier is held AND modifier is Shift:
    delta = (short)HIWORD(mouseData)    // horizontal delta (inverted convention)
    delta = -delta                       // negate to restore vertical semantics
    accumulate(delta)
    consume event
```

Key details:
- Only intercept `WM_MOUSEHWHEEL` when Shift is the active modifier. When Win/Ctrl/Alt is the modifier, `WM_MOUSEHWHEEL` should pass through to applications normally (it's a legitimate horizontal scroll).
- The delta negation corrects the sign convention difference between vertical and horizontal scroll. Verify the correct direction empirically from the investigation logs — if "scroll up" produces a positive `WM_MOUSEHWHEEL` delta, then negate. If it produces negative, don't negate. **Let the logs drive this, don't guess.**
- The event must still be consumed (return 1) to prevent the horizontal scroll from reaching applications.

### If Scenario B: Hook receives `WM_MOUSEWHEEL` with negated delta

Simply negate the delta when the configured modifier is Shift:

```
If event is WM_MOUSEWHEEL and modifier is held:
    delta = (short)HIWORD(mouseData)
    if modifier is VK_SHIFT:
        delta = -delta
    accumulate(delta)
    consume event
```

### If Scenario C: Delta is correct in hook, inverted downstream

Check ZoomController's `applyScrollDelta()` and the scroll accumulator read in RenderLoop. This is unlikely but worth ruling out.

---

## Constraints

- **Hook callback must remain minimal and non-blocking** (R-05). The fix adds at most one conditional check and one negation — no allocations, no I/O, no function calls.
- **AC-2.1.02 must be preserved:** When the modifier is NOT held, both `WM_MOUSEWHEEL` and `WM_MOUSEHWHEEL` must pass through to applications with zero modification. The fix must not accidentally consume horizontal scroll events during normal (non-zoom) use.
- **Other modifiers must be unaffected:** The fix must be gated on the modifier being Shift specifically, or more precisely, on the condition that would cause the OS to perform the vertical-to-horizontal conversion. Test all four modifiers after the fix.
- **Remove diagnostic logging** after the investigation is complete (or gate it behind `SMOOTHZOOM_LOGGING`).

---

## Test Matrix

After implementing the fix, verify all cells in this matrix:

| Modifier | Physical Scroll Up | Physical Scroll Down | Scroll Without Modifier |
|---|---|---|---|
| **Win** | Zooms in | Zooms out | Passes through to app |
| **Ctrl** | Zooms in | Zooms out | Passes through to app |
| **Alt** | Zooms in | Zooms out | Passes through to app |
| **Shift** | Zooms in | Zooms out | Passes through to app |

Additionally verify:
- Shift+horizontal scroll (tilt wheel, if available) while Shift is the modifier: should this zoom or pass through? The safe behavior is to only process events that originated as vertical scroll — if distinguishable.
- With Win as modifier (default), Shift+Scroll in a browser: horizontal scroll should work normally, not be consumed by SmoothZoom.
- Precision Touchpad two-finger scroll with Shift held: same zoom direction as mouse wheel.

---

## Acceptance Criteria Reference

- **AC-2.1.01:** Scroll up = zoom in, scroll down = zoom out (with modifier held)
- **AC-2.1.02:** No interference with scroll when modifier is not held
- **AC-2.1.05:** Precision Touchpad produces same behavior as mouse wheel
- **R-07:** Shift+Scroll conflict is a known risk — this fix is the mitigation

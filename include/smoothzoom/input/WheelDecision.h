#pragma once
// =============================================================================
// SmoothZoom — WheelDecision
// The WH_MOUSE_LL wheel decision as a pure function: given a wheel message, its
// raw delta, the configured modifier and whether that modifier is held, decide
// whether to consume it and what to add to SharedState::scrollAccumulator.
//
// Extracted verbatim (behavior-preserving) from InputInterceptor's mouse hook so
// that the hook, the dev-tools trace replay (--bench) and unit tests all run the
// exact same decision. Header-only, no Win32 dependency (VK constants come from
// ModifierUtils.h, which defines them off-Windows). Hook-safe: no allocation,
// no I/O, a few comparisons.
// =============================================================================

#include "smoothzoom/input/ModifierUtils.h"
#include <cstdint>

namespace SmoothZoom
{

enum class WheelKind : uint8_t
{
    Vertical,    // WM_MOUSEWHEEL
    Horizontal,  // WM_MOUSEHWHEEL
};

struct WheelDecision
{
    bool    consume = false;          // return 1 from the hook (AC-2.1.02)
    int32_t accumulatorDelta = 0;     // fetch_add into scrollAccumulator when consumed
    bool    markWinUsedForZoom = false; // WinKeyManager::markUsedForZoom() (AC-2.1.16)
};

inline bool isWinModifierVK(int modifierVK)
{
    return modifierVK == VK_LWIN || modifierVK == VK_RWIN;
}

inline bool isShiftModifierVK(int modifierVK)
{
    return modifierVK == VK_LSHIFT || modifierVK == VK_RSHIFT || modifierVK == VK_SHIFT;
}

// Whether the hook needs to evaluate modifier state for this message at all.
// WM_MOUSEHWHEEL is only ever a zoom candidate when Shift is the modifier
// (Windows/drivers may convert Shift+Scroll into horizontal scroll, R-07);
// otherwise horizontal scroll passes straight through. Kept separate so the hook
// preserves its original lazy evaluation of the modifier test.
inline bool wheelIsZoomCandidate(WheelKind kind, int modifierVK)
{
    return kind == WheelKind::Vertical || isShiftModifierVK(modifierVK);
}

inline WheelDecision decideWheel(WheelKind kind, int16_t rawDelta, int modifierVK, bool modifierHeld)
{
    WheelDecision d;
    if (!wheelIsZoomCandidate(kind, modifierVK) || !modifierHeld)
        return d; // pass through

    int16_t delta = rawDelta;
    if (kind == WheelKind::Horizontal)
    {
        // WM_MOUSEHWHEEL uses the opposite sign convention (positive = right);
        // negate so scroll-up = zoom-in (AC-2.1.01). int16 negation matches the
        // original hook code exactly (including -32768 wrapping).
        delta = static_cast<int16_t>(-delta);
    }
    else
    {
        // Start-menu suppression only applies to the vertical path with Win.
        d.markWinUsedForZoom = isWinModifierVK(modifierVK);
    }

    d.consume = true;
    d.accumulatorDelta = delta;
    return d;
}

// Modifier-held test from a recorded/synthetic modifier snapshot (dev tools
// replay). The live hook uses its own key-tracking (isConfiguredModifierHeld);
// a trace records the physical Shift/Ctrl/Alt/Win state at each event instead.
inline bool modifierHeldFromState(int modifierVK, bool shift, bool ctrl, bool alt, bool win)
{
    switch (modifierVK)
    {
    case VK_LWIN: case VK_RWIN:             return win;
    case VK_LSHIFT: case VK_RSHIFT: case VK_SHIFT: return shift;
    case VK_LCONTROL: case VK_RCONTROL: case VK_CONTROL: return ctrl;
    case VK_LMENU: case VK_RMENU: case VK_MENU:  return alt;
    default: return false;
    }
}

} // namespace SmoothZoom

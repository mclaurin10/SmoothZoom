#pragma once
// =============================================================================
// SmoothZoom — KeyDecision
// The WH_KEYBOARD_LL zoom-shortcut decision as a pure function: given a key
// event, the configured modifier, whether that modifier is held and whether
// keyboard shortcuts are enabled, decide whether to consume the event and which
// zoom command (if any) to post.
//
// Extracted from InputInterceptor's keyboard hook, the same way WheelDecision.h
// was extracted from the mouse hook, so the hook and unit tests run the exact
// same decision. Covers every shortcut the "Keyboard shortcuts" setting gates
// (AC-2.8.13–15): modifier + Plus/Minus/Esc (AC-2.8.01–10), the Ctrl+Alt peek
// toggle (AC-2.7.x) and Ctrl+Alt+I (AC-2.10.01). Key-state tracking and
// Win+Ctrl+M (settings, never gated) stay in the hook.
// Header-only, no Win32 dependency (VK constants come from ModifierUtils.h).
// Hook-safe: no allocation, no I/O, a few comparisons.
// =============================================================================

#include "smoothzoom/input/ModifierUtils.h"
#include <cstdint>

namespace SmoothZoom
{

enum class ShortcutAction : uint8_t
{
    None,
    ZoomIn,     // modifier + VK_OEM_PLUS / VK_ADD
    ZoomOut,    // modifier + VK_OEM_MINUS / VK_SUBTRACT
    ResetZoom,  // modifier + VK_ESCAPE
};

struct KeyDecision
{
    bool           consume = false;            // return 1 from the hook
    ShortcutAction action = ShortcutAction::None; // command to post (key-down only)
    bool           markWinUsedForZoom = false; // WinKeyManager::markUsedForZoom() (AC-2.1.16)
};

// The four keys that produce a character ('=' '+' '-' '_') and are therefore
// consumed while the modifier is held, on both edges.
inline bool isZoomStepKey(int vkCode)
{
    return vkCode == VK_OEM_PLUS || vkCode == VK_ADD
        || vkCode == VK_OEM_MINUS || vkCode == VK_SUBTRACT;
}

// `injected` (LLKHF_INJECTED) deliberately does not change the outcome: peripheral
// macros (Logitech Options+, AHK) send Shift+= / Shift+- through SendInput, and
// those must be consumed too or the character leaks. WinKeyManager only ever
// injects VK_CONTROL, which is never consumed, so there is no self-feedback.
// The parameter exists so the tests can pin that injected events behave the same.
inline KeyDecision decideShortcutKey(int vkCode, bool isDown, bool isUp, bool injected,
                                     int modifierVK, bool modifierHeld, bool shortcutsEnabled)
{
    (void)injected;
    KeyDecision d;

    // Shortcuts off: every key passes through and no command is posted.
    if (!shortcutsEnabled || !modifierHeld)
        return d;

    switch (vkCode)
    {
    case VK_OEM_PLUS:   // '=' / '+' on main keyboard
    case VK_ADD:        // '+' on numpad
        if (isDown) d.action = ShortcutAction::ZoomIn;
        d.consume = isDown || isUp;  // prevent character insertion
        break;

    case VK_OEM_MINUS:  // '-' on main keyboard
    case VK_SUBTRACT:   // '-' on numpad
        if (isDown) d.action = ShortcutAction::ZoomOut;
        d.consume = isDown || isUp;
        break;

    case VK_ESCAPE:     // produces no character; apps may need it, so never consumed
        if (isDown) d.action = ShortcutAction::ResetZoom;
        break;

    default:            // includes the modifier keys themselves: never consumed
        break;
    }

    d.markWinUsedForZoom = (d.action != ShortcutAction::None) && isWinModifierVK(modifierVK);
    return d;
}

// ── Ctrl+Alt hold-to-peek (AC-2.7.01–10) ─────────────────────────────────────
// The hook always tracks whether both toggle keys are held; this decides which
// edge to post. With shortcuts off the toggle never engages, and an engaged
// toggle releases on the next key event (the hook's settings observer also
// releases it at once when the setting is turned off).
enum class ToggleEdge : uint8_t { None, Engage, Release };

inline ToggleEdge decideToggleChord(bool bothToggleKeysHeld, bool engaged, bool shortcutsEnabled)
{
    const bool want = shortcutsEnabled && bothToggleKeysHeld;
    if (want && !engaged) return ToggleEdge::Engage;
    if (!want && engaged) return ToggleEdge::Release;
    return ToggleEdge::None;
}

// ── Ctrl+Alt+I color inversion (AC-2.10.01) ──────────────────────────────────
// Edge-triggered: chordAlreadyDown is the hook's typematic filter (set on the
// first qualifying 'I' down, cleared on 'I' up). Never consumed either way.
inline bool decideInvertChord(int vkCode, bool isDown, bool ctrlHeld, bool altHeld,
                              bool chordAlreadyDown, bool shortcutsEnabled)
{
    return shortcutsEnabled && isDown && vkCode == 'I'
        && ctrlHeld && altHeld && !chordAlreadyDown;
}

} // namespace SmoothZoom

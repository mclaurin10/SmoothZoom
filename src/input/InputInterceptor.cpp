// =============================================================================
// SmoothZoom — InputInterceptor
// Global low-level hooks: WH_MOUSE_LL and WH_KEYBOARD_LL. Doc 3 §3.1
//
// CRITICAL: Hook callbacks must be minimal and non-blocking (R-05).
// The system silently deregisters hooks that exceed ~300ms.
// Callbacks: read event → update atomic or post message → return.
// No computation, no I/O, no allocation.
//
// Phase 1: WinKeyManager integration for Start Menu suppression (AC-2.1.16).
// Phase 5B: Configurable modifier/toggle keys, Win+Ctrl+M shortcut.
// =============================================================================

#include "smoothzoom/input/InputInterceptor.h"
#include "smoothzoom/input/WinKeyManager.h"
#include "smoothzoom/input/ModifierUtils.h"
#include "smoothzoom/common/AppMessages.h"
#include "smoothzoom/common/SharedState.h"
#include "smoothzoom/common/Types.h"
#include "smoothzoom/support/SettingsManager.h"
#include "smoothzoom/support/Logger.h"

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <windows.h>
#include <chrono>

namespace SmoothZoom
{

// Static state — hook callbacks are static C functions, can't use `this`.
static SharedState* s_state = nullptr;
static HHOOK s_mouseHook = nullptr;
static HHOOK s_keyboardHook = nullptr;
static WinKeyManager s_winKeyMgr;

// Hook liveness stamp for the R-05 watchdog (GetTickCount64 domain).
// Silent OS deregistration leaves the HHOOK non-null, so handle checks alone
// cannot detect it — the watchdog compares this against GetLastInputInfo.
// Hooks and watchdog both run on the main thread: plain int64_t, no atomic.
static int64_t s_lastHookCallbackTick = 0;

// Steady-clock ms — same time base as RenderLoop's currentTimeMs() and
// FocusMonitor's lastFocusChangeTime. KBDLLHOOKSTRUCT::time is in the
// GetTickCount domain, which diverges from steady_clock across suspend/resume
// and wraps at 49.7 days — mixing the two silently broke the 500ms
// caret-priority window. A QPC read is ~20ns: hook-safe.
static int64_t steadyNowMs()
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

// Phase 5B: Configurable keys — updated by settings observer on main thread.
// Safe to read from hook callbacks (same thread). (AC-2.1.19, AC-2.1.20)
static int s_modifierKeyVK = VK_LWIN;
static int s_toggleKey1VK  = VK_LCONTROL;
static int s_toggleKey2VK  = VK_LMENU;
static HWND s_msgWindow    = nullptr;

// Tracks non-Win modifier key state via keyboard hook for reliable detection
// in mouse hook. Both hooks run on the main thread — no synchronization needed.
static bool s_nonWinModifierHeld = false;

// Phase 5B: Settings observer callback — runs on main thread (same as hooks).
static void onSettingsChanged(const SettingsSnapshot& s, void* /*userData*/)
{
    s_modifierKeyVK = s.modifierKeyVK;
    s_toggleKey1VK  = s.toggleKey1VK;
    s_toggleKey2VK  = s.toggleKey2VK;
    s_nonWinModifierHeld = false;  // Reset on config change to avoid stale state
    s_winKeyMgr.reset();  // BF-2: Clear stale Win key state on modifier change
}

// WM_OPEN_SETTINGS defined in common/AppMessages.h

// Helper: check if the configured modifier key is currently held.
// Mirrors the scroll-gesture modifier check (AC-2.1.19, AC-2.1.20).
// Win modifier → WinKeyManager state machine; others → hook state OR GetAsyncKeyState.
// GetAsyncKeyState fallback is needed because some touchpad drivers send scroll events
// without corresponding keyboard hook events for modifier keys (e.g., Shift+touchpad
// scroll may bypass the LL keyboard hook when certain apps are focused).
static bool isConfiguredModifierHeld()
{
    if (s_modifierKeyVK == VK_LWIN || s_modifierKeyVK == VK_RWIN)
    {
        // Cross-check physical key state. The state machine alone sticks at
        // Held* whenever the Win key-up is never delivered — deterministically
        // on Win+L, where the key-up lands on the secure desktop and LL hooks
        // receive nothing. Without this check, every plain mouse-wheel scroll
        // after unlock is consumed and zooms the screen until Win is re-tapped.
        // GetAsyncKeyState is a fast user32 read — hook-safe.
        bool physicallyHeld =
            ((GetAsyncKeyState(VK_LWIN) | GetAsyncKeyState(VK_RWIN)) & 0x8000) != 0;
        return physicallyHeld && s_winKeyMgr.state() != WinKeyManager::State::Idle;
    }
    return s_nonWinModifierHeld
        || (GetAsyncKeyState(toGenericVK(s_modifierKeyVK)) & 0x8000) != 0;
}

// Helper: check if the configured modifier is the Win key.
static bool isWinModifier()
{
    return s_modifierKeyVK == VK_LWIN || s_modifierKeyVK == VK_RWIN;
}

// ─── Mouse Hook Callback ────────────────────────────────────────────────────
// Minimal: read event, check modifier via WinKeyManager, update atomics, return.
static LRESULT CALLBACK mouseHookProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    s_lastHookCallbackTick = static_cast<int64_t>(GetTickCount64()); // R-05 liveness

    if (nCode < 0 || s_state == nullptr)
        return CallNextHookEx(s_mouseHook, nCode, wParam, lParam);

    auto* info = reinterpret_cast<MSLLHOOKSTRUCT*>(lParam);

    switch (wParam)
    {
    case WM_MOUSEWHEEL:
    {
#ifdef SMOOTHZOOM_LOGGING
        {
            wchar_t dbg[256];
            swprintf_s(dbg, L"[SZ-DIAG] WM_MOUSEWHEEL: nonWinMod=%d, modVK=0x%X, asyncMod=%d, fg=0x%p\n",
                s_nonWinModifierHeld ? 1 : 0,
                s_modifierKeyVK,
                (GetAsyncKeyState(toGenericVK(s_modifierKeyVK)) & 0x8000) ? 1 : 0,
                GetForegroundWindow());
            OutputDebugStringW(dbg);
        }
#endif
        // Phase 5B: Configurable modifier key (AC-2.1.19, AC-2.1.20)
        bool modifierHeld = isConfiguredModifierHeld();

        if (modifierHeld)
        {
            // Extract scroll delta from high word of mouseData
            int16_t delta = static_cast<int16_t>(HIWORD(info->mouseData));

            // Record LL hook scroll timestamp for Raw Input dedup
            s_state->lastLLHookScrollTime.store(
                static_cast<int64_t>(GetTickCount64()), std::memory_order_relaxed);

            // Atomically accumulate delta (render thread will exchange-with-0)
            s_state->scrollAccumulator.fetch_add(delta, std::memory_order_release);

            // Suppress Start Menu only when Win is the modifier (AC-2.1.16, AC-2.1.20)
            if (s_modifierKeyVK == VK_LWIN || s_modifierKeyVK == VK_RWIN)
                s_winKeyMgr.markUsedForZoom();

            s_state->modifierHeld.store(true, std::memory_order_relaxed);

            // Consume the event — do not pass to next hook or applications (AC-2.1.02)
            return 1;
        }
        break;
    }

    case WM_MOUSEHWHEEL:
    {
        // Windows (and some mouse drivers) convert Shift+Scroll into horizontal
        // scroll (WM_MOUSEHWHEEL) before it reaches low-level hooks. Only
        // intercept when Shift IS the configured modifier — otherwise let
        // horizontal scroll pass through to applications normally (AC-2.1.02).
        // R-07: Shift+Scroll conflict mitigation.
        bool isShiftMod = (s_modifierKeyVK == VK_LSHIFT || s_modifierKeyVK == VK_RSHIFT
                           || s_modifierKeyVK == VK_SHIFT);

        if (isShiftMod)
        {
#ifdef SMOOTHZOOM_LOGGING
            {
                wchar_t dbg[256];
                swprintf_s(dbg, L"[SZ-DIAG] WM_MOUSEHWHEEL (Shift mod): nonWinMod=%d, modVK=0x%X, asyncMod=%d, fg=0x%p\n",
                    s_nonWinModifierHeld ? 1 : 0,
                    s_modifierKeyVK,
                    (GetAsyncKeyState(toGenericVK(s_modifierKeyVK)) & 0x8000) ? 1 : 0,
                    GetForegroundWindow());
                OutputDebugStringW(dbg);
            }
#endif
            bool modifierHeld = isConfiguredModifierHeld();

            if (modifierHeld)
            {
                int16_t delta = static_cast<int16_t>(HIWORD(info->mouseData));

                // Negate: WM_MOUSEHWHEEL uses opposite sign convention from
                // WM_MOUSEWHEEL (positive = right, not up). Restores vertical
                // scroll semantics so scroll-up = zoom-in (AC-2.1.01).
                delta = -delta;

                // Record LL hook scroll timestamp for Raw Input dedup
                s_state->lastLLHookScrollTime.store(
                    static_cast<int64_t>(GetTickCount64()), std::memory_order_relaxed);

                s_state->scrollAccumulator.fetch_add(delta, std::memory_order_release);

                s_state->modifierHeld.store(true, std::memory_order_relaxed);

                // Consume — prevent horizontal scroll reaching applications
                return 1;
            }
        }
        break;
    }

    default:
        break;
    }

    // Pass through to next hook
    return CallNextHookEx(s_mouseHook, nCode, wParam, lParam);
}

// ─── Keyboard Hook Callback ─────────────────────────────────────────────────
// Tracks Win key via WinKeyManager. Consumes zoom-in/out keys only when the
// configured modifier is held (prevents character leak with Shift modifier);
// all other keyboard events pass through. (AC-2.1.18)
// Phase 4: Tracks Ctrl+Alt for temporary toggle (AC-2.7.01–AC-2.7.10).

// Phase 4/5B: Toggle key state — configurable keys (3 bools + 1 queue push), R-05 safe.
static bool s_toggleKey1Held = false;
static bool s_toggleKey2Held = false;
static bool s_toggleEngaged = false;

// Ctrl+Alt+I edge filter: LL hooks receive typematic auto-repeats and
// KBDLLHOOKSTRUCT carries no repeat flag. Without edge-triggering, holding the
// chord strobes full-screen inversion at the keyboard repeat rate — a
// photosensitivity hazard. Set on the first qualifying 'I' down, cleared on 'I' up.
static bool s_invertChordDown = false;

static LRESULT CALLBACK keyboardHookProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    s_lastHookCallbackTick = static_cast<int64_t>(GetTickCount64()); // R-05 liveness

    if (nCode < 0 || s_state == nullptr)
        return CallNextHookEx(s_keyboardHook, nCode, wParam, lParam);

    auto* info = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);

    bool isDown = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN);
    bool isUp = (wParam == WM_KEYUP || wParam == WM_SYSKEYUP);

    // Track Win key state via WinKeyManager (AC-2.1.04: both LWin and RWin)
    if (info->vkCode == VK_LWIN || info->vkCode == VK_RWIN)
    {
        if (isDown)
        {
            s_winKeyMgr.onWinKeyDown();
        }
        else if (isUp)
        {
            s_winKeyMgr.onWinKeyUp();
            // BF-3: Only clear modifierHeld on Win key-up when Win IS the modifier.
            // Otherwise, releasing Win while Alt/Shift is held would incorrectly
            // clear the modifier state.
            if (isWinModifier())
                s_state->modifierHeld.store(false, std::memory_order_relaxed);
        }
    }

    // Track non-Win modifier state for scroll gesture detection (Issue 1 fix)
    if (!isWinModifier() && isModifierMatch(static_cast<int>(info->vkCode), s_modifierKeyVK))
    {
#ifdef SMOOTHZOOM_LOGGING
        {
            wchar_t dbg[128];
            swprintf_s(dbg, L"[SZ-DIAG] ModifierKey vk=0x%X isDown=%d\n",
                info->vkCode, isDown ? 1 : 0);
            OutputDebugStringW(dbg);
        }
#endif
        s_nonWinModifierHeld = isDown;
        if (isUp && s_state)
            s_state->modifierHeld.store(false, std::memory_order_relaxed);
    }

    // Phase 4/5B: Configurable toggle key tracking (AC-2.7.01–AC-2.7.10)
    bool isToggle1 = isModifierMatch(info->vkCode, s_toggleKey1VK);
    bool isToggle2 = isModifierMatch(info->vkCode, s_toggleKey2VK);

    if (isToggle1) s_toggleKey1Held = isDown;
    if (isToggle2) s_toggleKey2Held = isDown;

    bool bothHeld = s_toggleKey1Held && s_toggleKey2Held;
    if (bothHeld && !s_toggleEngaged)
    {
        s_toggleEngaged = true;
        s_state->commandQueue.push(ZoomCommand::ToggleEngage);
    }
    if (!bothHeld && s_toggleEngaged)
    {
        s_toggleEngaged = false;
        s_state->commandQueue.push(ZoomCommand::ToggleRelease);
    }

    // Clear the inversion-chord edge filter on 'I' release (see s_invertChordDown)
    if (info->vkCode == 'I' && isUp)
        s_invertChordDown = false;

    if (isDown)
    {
        // Record keyboard activity timestamp (for caret tracking priority — Phase 3)
        // Filter out modifier keys — holding Ctrl/Alt/Shift/Win alone should not
        // activate caret tracking (WS2C fix).
        if (!isModifierVK(static_cast<int>(info->vkCode)))
        {
            // steadyNowMs, not info->time: the arbitration compares this against
            // steady_clock timestamps (see steadyNowMs comment above).
            s_state->lastKeyboardInputTime.store(
                steadyNowMs(), std::memory_order_relaxed);
        }

        // Modifier+key shortcuts (Phase 2: AC-2.8.01–AC-2.8.10, Phase 5B: AC-2.1.19)
        // Uses configurable modifier — not hardcoded to Win key.
        bool modHeld = isConfiguredModifierHeld();
        if (modHeld)
        {
            switch (info->vkCode)
            {
            case VK_OEM_PLUS:   // '=' / '+' on main keyboard
            case VK_ADD:        // '+' on numpad
                s_state->commandQueue.push(ZoomCommand::ZoomIn);
                if (isWinModifier()) s_winKeyMgr.markUsedForZoom();
                break;

            case VK_OEM_MINUS:  // '-' on main keyboard
            case VK_SUBTRACT:   // '-' on numpad
                s_state->commandQueue.push(ZoomCommand::ZoomOut);
                if (isWinModifier()) s_winKeyMgr.markUsedForZoom();
                break;

            case VK_ESCAPE:
                s_state->commandQueue.push(ZoomCommand::ResetZoom);
                if (isWinModifier()) s_winKeyMgr.markUsedForZoom();
                break;
            }
        }

        // Win+Ctrl+M → open settings (AC-2.8.11)
        // Always Win-based regardless of configured modifier.
        // Physical Win check guards against a stale state machine (missed
        // key-up across the secure desktop) turning plain Ctrl+M into this.
        if (info->vkCode == 'M'
            && s_winKeyMgr.state() != WinKeyManager::State::Idle
            && ((GetAsyncKeyState(VK_LWIN) | GetAsyncKeyState(VK_RWIN)) & 0x8000)
            && (GetAsyncKeyState(VK_CONTROL) & 0x8000)
            && s_msgWindow)
        {
            PostMessageW(s_msgWindow, WM_OPEN_SETTINGS, 0, 0);
            s_winKeyMgr.markUsedForZoom();
        }

        // Ctrl+Alt+I → toggle color inversion (Phase 6: AC-2.10.01)
        // Edge-triggered via s_invertChordDown — see declaration comment.
        if (info->vkCode == 'I')
        {
            bool ctrlHeld = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
            bool altHeld  = (GetAsyncKeyState(VK_MENU)    & 0x8000) != 0;
            if (ctrlHeld && altHeld && !s_invertChordDown)
            {
                s_invertChordDown = true;
                s_state->commandQueue.push(ZoomCommand::ToggleInvert);
            }
        }

        // Ctrl+Q → graceful exit (Phase 5C: AC-2.9.16)
        if (info->vkCode == 'Q')
        {
            bool ctrlHeld = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
            if (ctrlHeld && s_msgWindow)
            {
                PostMessageW(s_msgWindow, WM_GRACEFUL_EXIT, 0, 0);
            }
        }
    }

    // Consume zoom-in/zoom-out action keys when the configured modifier is held.
    // Required for non-Win modifiers (e.g., Shift) where Shift+= produces '+' and
    // Shift+- produces '_', leaking into focused text fields. Also required for
    // peripheral macros (Logitech Options+, AHK, etc.) that send Shift+=/Shift+-
    // via SendInput — those events have LLKHF_INJECTED set but must still be
    // consumed, otherwise the character leaks into the focused app. The zoom
    // command was already posted above (lines 278-299) on key-down; this gate
    // blocks the keystroke from reaching applications on BOTH key-down and key-up.
    //
    // Why only zoom-in/out (not Esc/settings/toggle/inversion):
    //   - VK_ESCAPE produces no character — safe to pass through (apps may need it).
    //   - Win+Ctrl+M, Ctrl+Alt+I, Ctrl+Q, Ctrl+Alt toggle — all use Ctrl/Alt/Win
    //     which never produce printable characters. Consuming them could break
    //     standard app behavior (e.g., Esc closing dialogs).
    //
    // No LLKHF_INJECTED guard: WinKeyManager only injects VK_CONTROL, never any
    // of the four zoom keys below, so consuming injected variants is safe and
    // required for peripheral-macro use cases.
    if ((isDown || isUp) && isConfiguredModifierHeld())
    {
        switch (info->vkCode)
        {
        case VK_OEM_PLUS:   // '=' / '+' on main keyboard
        case VK_ADD:        // '+' on numpad
        case VK_OEM_MINUS:  // '-' on main keyboard
        case VK_SUBTRACT:   // '-' on numpad
            return 1;       // Consume — prevent character insertion
        }
    }

    // Never consume non-zoom keyboard events (Doc 3 §3.1, AC-2.1.18)
    return CallNextHookEx(s_keyboardHook, nCode, wParam, lParam);
}

// ─── Public Interface ───────────────────────────────────────────────────────

bool InputInterceptor::install(SharedState& state)
{
    s_state = &state;
    s_winKeyMgr = WinKeyManager{}; // Reset state machine

    // Install low-level mouse hook (requires message pump on this thread)
    s_mouseHook = SetWindowsHookExW(WH_MOUSE_LL, mouseHookProc, nullptr, 0);
    if (!s_mouseHook)
        return false;

    // Install low-level keyboard hook
    s_keyboardHook = SetWindowsHookExW(WH_KEYBOARD_LL, keyboardHookProc, nullptr, 0);
    if (!s_keyboardHook)
    {
        UnhookWindowsHookEx(s_mouseHook);
        s_mouseHook = nullptr;
        return false;
    }

    // Seed liveness so the watchdog has a grace period before any input arrives
    s_lastHookCallbackTick = static_cast<int64_t>(GetTickCount64());

    return true;
}

void InputInterceptor::uninstall()
{
    if (s_mouseHook)
    {
        UnhookWindowsHookEx(s_mouseHook);
        s_mouseHook = nullptr;
    }
    if (s_keyboardHook)
    {
        UnhookWindowsHookEx(s_keyboardHook);
        s_keyboardHook = nullptr;
    }
    s_state = nullptr;
}

bool InputInterceptor::isHealthy() const
{
    return s_mouseHook != nullptr && s_keyboardHook != nullptr;
}

bool InputInterceptor::reinstall()
{
    if (s_state == nullptr)
        return false;

    // Reinstall unhealthy hooks (R-05, AC-ERR.03)
    if (!s_mouseHook)
    {
        s_mouseHook = SetWindowsHookExW(WH_MOUSE_LL, mouseHookProc, nullptr, 0);
    }
    if (!s_keyboardHook)
    {
        s_keyboardHook = SetWindowsHookExW(WH_KEYBOARD_LL, keyboardHookProc, nullptr, 0);
    }

    return isHealthy();
}

bool InputInterceptor::forceReinstall()
{
    if (s_state == nullptr)
        return false;

    // Silent OS deregistration (R-05) leaves the HHOOK non-null but dead, so
    // unconditionally unhook and rehook. UnhookWindowsHookEx on an
    // already-deregistered handle fails harmlessly.
    if (s_mouseHook)
    {
        UnhookWindowsHookEx(s_mouseHook);
        s_mouseHook = nullptr;
    }
    if (s_keyboardHook)
    {
        UnhookWindowsHookEx(s_keyboardHook);
        s_keyboardHook = nullptr;
    }
    s_mouseHook = SetWindowsHookExW(WH_MOUSE_LL, mouseHookProc, nullptr, 0);
    s_keyboardHook = SetWindowsHookExW(WH_KEYBOARD_LL, keyboardHookProc, nullptr, 0);

    // Key-up events were likely missed during the outage — clear held-key state.
    resetTransientKeyState();

    // Restart the liveness grace period
    s_lastHookCallbackTick = static_cast<int64_t>(GetTickCount64());

    return isHealthy();
}

int64_t InputInterceptor::lastCallbackTick()
{
    return s_lastHookCallbackTick;
}

void InputInterceptor::resetTransientKeyState()
{
    // Clears every held/engaged flag derived from hook-delivered key events.
    // Called when key-ups may have been missed: secure-desktop transitions
    // (Win+L strands WinKeyManager in Held*, hijacking plain scrolling into
    // zoom after unlock) and hook reinstalls after an outage.
    s_winKeyMgr.reset();
    s_nonWinModifierHeld = false;
    s_toggleKey1Held = false;
    s_toggleKey2Held = false;
    s_invertChordDown = false;
    if (s_toggleEngaged)
    {
        s_toggleEngaged = false;
        if (s_state)
            s_state->commandQueue.push(ZoomCommand::ToggleRelease);
    }
    if (s_state)
        s_state->modifierHeld.store(false, std::memory_order_relaxed);
}

// Phase 5B: Register for settings change notifications (AC-2.9.04)
void InputInterceptor::registerSettingsObserver(SettingsManager& mgr)
{
    mgr.addObserver(onSettingsChanged, nullptr);
}

// Phase 5B: Store message window handle for Win+Ctrl+M posting (AC-2.8.11)
void InputInterceptor::setMessageWindow(void* hWnd)
{
    s_msgWindow = static_cast<HWND>(hWnd);
}

} // namespace SmoothZoom

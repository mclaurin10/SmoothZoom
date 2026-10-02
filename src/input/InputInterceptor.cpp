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
#include "smoothzoom/input/WheelDecision.h"
#include "smoothzoom/input/KeyDecision.h"
#include "smoothzoom/input/StickyKeysPolicy.h"
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
static int s_modifierKeyVK = kDefaultModifierVK;  // until the first settings notification
static int s_toggleKey1VK  = VK_LCONTROL;
static int s_toggleKey2VK  = VK_LMENU;
static HWND s_msgWindow    = nullptr;

// Keyboard zoom shortcuts on/off (tray "Keyboard shortcuts", AC-2.8.13–15):
// modifier + Plus/Minus/Esc, the Ctrl+Alt peek toggle and Ctrl+Alt+I. The
// keyboard hook stays installed while off — it still tracks the scroll
// modifier (Shift+scroll needs it), the toggle keys, Win+Ctrl+M and
// caret-priority keystrokes — but consumes nothing and posts no zoom command.
static bool s_shortcutsEnabled = false;

// Tracks non-Win modifier key state via keyboard hook for reliable detection
// in mouse hook. Both hooks run on the main thread — no synchronization needed.
static bool s_nonWinModifierHeld = false;

// ─── Sticky Keys shortcut suppression (AC-2.1.23/24, R-31) ──────────────────
// Main thread only (settings observer, uninstall); never from a hook callback.
// POD statics so the crash handler can restore without allocating.
static bool     s_stickySuppressed = false;
static uint32_t s_stickyUserFlags  = 0;   // user's flags when suppression began

// The user's own setting, from the profile (SmoothZoom never writes it).
static bool readPersistedStickyFlags(uint32_t& out)
{
    wchar_t buf[32] = {};
    DWORD cb = sizeof(buf);
    if (RegGetValueW(HKEY_CURRENT_USER, L"Control Panel\\Accessibility\\StickyKeys", L"Flags",
                     RRF_RT_REG_SZ, nullptr, buf, &cb) != ERROR_SUCCESS)
        return false;
    return parseStickyFlagsString(buf, out);
}

static bool restoreStickyShortcut(bool useProfile)
{
    if (!s_stickySuppressed)
        return true;
    STICKYKEYS sk = { sizeof(sk) };
    if (!SystemParametersInfoW(SPI_GETSTICKYKEYS, sizeof(sk), &sk, 0))
        return false;
    uint32_t user = s_stickyUserFlags;
    if (useProfile)
    {
        uint32_t persisted = 0;
        if (readPersistedStickyFlags(persisted))
            user = persisted;  // reflects any change the user made in Settings meanwhile
    }
    sk.dwFlags = restoredStickyFlags(sk.dwFlags, user);
    // fWinIni = 0: in-memory only, the profile is never written.
    if (!SystemParametersInfoW(SPI_SETSTICKYKEYS, sizeof(sk), &sk, 0))
        return false;
    s_stickySuppressed = false;
    return true;
}

static void applyStickyShortcutPolicy(bool wanted)
{
    if (!wanted)
    {
        if (s_stickySuppressed && restoreStickyShortcut(true))
            SZ_LOG_INFO("InputInterceptor", L"Sticky Keys shortcut restored");
        return;
    }
    if (s_stickySuppressed)
        return;

    STICKYKEYS sk = { sizeof(sk) };
    if (!SystemParametersInfoW(SPI_GETSTICKYKEYS, sizeof(sk), &sk, 0))
        return;
    if (!stickyKeysMayBeChanged(sk.dwFlags))
    {
        SZ_LOG_INFO("InputInterceptor", L"Sticky Keys is on - shortcut left unchanged");
        return;
    }
    // Restore target = the profile value when readable. After a previous run was
    // killed mid-suppression, the in-memory flags still have the shortcut off,
    // but the profile does not, so this also repairs that case on exit.
    uint32_t user = sk.dwFlags;
    uint32_t persisted = 0;
    if (readPersistedStickyFlags(persisted))
        user = persisted;
    s_stickyUserFlags = user;

    sk.dwFlags = suppressedStickyFlags(sk.dwFlags);
    if (SystemParametersInfoW(SPI_SETSTICKYKEYS, sizeof(sk), &sk, 0))  // fWinIni = 0
    {
        s_stickySuppressed = true;
        SZ_LOG_INFO("InputInterceptor", L"Sticky Keys shortcut suppressed (user flags 0x%X)", user);
    }
}

// Phase 4/5B: Toggle key state — configurable keys (3 bools + 1 queue push),
// R-05 safe. Declared here so the settings observer can release an engaged
// peek when shortcuts are turned off.
static bool s_toggleKey1Held = false;
static bool s_toggleKey2Held = false;
static bool s_toggleEngaged = false;

// Phase 5B: Settings observer callback — runs on main thread (same as hooks).
static void onSettingsChanged(const SettingsSnapshot& s, void* /*userData*/)
{
    s_modifierKeyVK = s.modifierKeyVK;
    s_toggleKey1VK  = s.toggleKey1VK;
    s_toggleKey2VK  = s.toggleKey2VK;
    s_shortcutsEnabled = s.keyboardShortcutsEnabled;
    applyStickyShortcutPolicy(stickyShortcutSuppressionWanted(s.blockStickyKeysShortcut, s.modifierKeyVK));
    if (!s_shortcutsEnabled && s_toggleEngaged)
    {
        // Turning shortcuts off mid-peek: end the peek now (AC-2.8.14).
        s_toggleEngaged = false;
        if (s_state)
            s_state->commandQueue.push(ZoomCommand::ToggleRelease);
    }
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

// Side effects of a consumed wheel event (shared by the hook and --bench replay).
// Hook-safe: two atomic writes and a WinKeyManager flag (no-op unless Win is held).
static void applyWheelDecision(const WheelDecision& d)
{
    // Record LL hook scroll timestamp for Raw Input dedup
    s_state->lastLLHookScrollTime.store(
        static_cast<int64_t>(GetTickCount64()), std::memory_order_relaxed);

    // Atomically accumulate delta (render thread will exchange-with-0)
    s_state->scrollAccumulator.fetch_add(d.accumulatorDelta, std::memory_order_release);

    // Suppress Start Menu only when Win is the modifier (AC-2.1.16, AC-2.1.20)
    if (d.markWinUsedForZoom)
        s_winKeyMgr.markUsedForZoom();
}

#ifdef SMOOTHZOOM_DEV_TOOLS
// --record tap. Runs inside the hook: fills a POD on the stack and hands it to
// the tap, which copies it into a pre-allocated ring (DevTools.cpp). The extra
// GetAsyncKeyState reads exist only in dev builds with recording active.
static WheelTapFn s_wheelTap = nullptr;

static void tapHookWheel(WheelKind kind, int16_t rawDelta, const MSLLHOOKSTRUCT* info,
                         bool modifierHeld, bool consumed)
{
    WheelTapEvent e;
    LARGE_INTEGER q;
    QueryPerformanceCounter(&q);
    e.qpc = q.QuadPart;
    e.x = info->pt.x;
    e.y = info->pt.y;
    e.flags = info->flags;
    e.delta = rawDelta;
    e.source = (kind == WheelKind::Vertical) ? WheelSource::HookWheel : WheelSource::HookHWheel;
    e.mods = static_cast<uint8_t>(
        ((GetAsyncKeyState(VK_SHIFT) & 0x8000) ? kModShift : 0)
        | ((GetAsyncKeyState(VK_CONTROL) & 0x8000) ? kModCtrl : 0)
        | ((GetAsyncKeyState(VK_MENU) & 0x8000) ? kModAlt : 0)
        | (((GetAsyncKeyState(VK_LWIN) | GetAsyncKeyState(VK_RWIN)) & 0x8000) ? kModWin : 0));
    e.modifierHeld = modifierHeld;
    e.consumed = consumed;
    s_wheelTap(e);
}
#endif

// ─── Mouse Hook Callback ────────────────────────────────────────────────────
// Minimal: read event, check modifier via WinKeyManager, update atomics, return.
static LRESULT CALLBACK mouseHookProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    s_lastHookCallbackTick = static_cast<int64_t>(GetTickCount64()); // R-05 liveness

    if (nCode < 0 || s_state == nullptr)
        return CallNextHookEx(s_mouseHook, nCode, wParam, lParam);

    auto* info = reinterpret_cast<MSLLHOOKSTRUCT*>(lParam);

    if (wParam == WM_MOUSEWHEEL || wParam == WM_MOUSEHWHEEL)
    {
        const WheelKind kind = (wParam == WM_MOUSEWHEEL) ? WheelKind::Vertical
                                                         : WheelKind::Horizontal;
        const int16_t rawDelta = static_cast<int16_t>(HIWORD(info->mouseData));

        // Windows (and some mouse drivers) may convert Shift+Scroll into
        // horizontal scroll (WM_MOUSEHWHEEL) before it reaches low-level hooks,
        // so WM_MOUSEHWHEEL is a zoom candidate only when Shift IS the
        // configured modifier; otherwise horizontal scroll passes through
        // untouched (AC-2.1.02, R-07). The modifier test is evaluated only for
        // candidates, as before the WheelDecision extraction.
        const bool candidate = wheelIsZoomCandidate(kind, s_modifierKeyVK);

#ifdef SMOOTHZOOM_INPUT_DIAG  // opt-in per-event hook tracing — deliberately NOT enabled by Debug/SMOOTHZOOM_LOGGING (R-05: hook callbacks must do no I/O)
        if (candidate)
        {
            wchar_t dbg[256];
            swprintf_s(dbg, L"[SZ-DIAG] %s: nonWinMod=%d, modVK=0x%X, asyncMod=%d, fg=0x%p\n",
                kind == WheelKind::Vertical ? L"WM_MOUSEWHEEL" : L"WM_MOUSEHWHEEL (Shift mod)",
                s_nonWinModifierHeld ? 1 : 0,
                s_modifierKeyVK,
                (GetAsyncKeyState(toGenericVK(s_modifierKeyVK)) & 0x8000) ? 1 : 0,
                GetForegroundWindow());
            OutputDebugStringW(dbg);
        }
#endif
        // Phase 5B: Configurable modifier key (AC-2.1.19, AC-2.1.20)
        const bool modifierHeld = candidate && isConfiguredModifierHeld();
        const WheelDecision d = decideWheel(kind, rawDelta, s_modifierKeyVK, modifierHeld);

#ifdef SMOOTHZOOM_DEV_TOOLS
        if (s_wheelTap)
            tapHookWheel(kind, rawDelta, info, modifierHeld, d.consume);
#endif

        if (d.consume)
        {
            applyWheelDecision(d);
            // Consume the event — do not pass to next hook or applications (AC-2.1.02)
            return 1;
        }
    }

    // Pass through to next hook
    return CallNextHookEx(s_mouseHook, nCode, wParam, lParam);
}

// ─── Keyboard Hook Callback ─────────────────────────────────────────────────
// Tracks Win key via WinKeyManager (Win modifier only). When keyboard shortcuts
// are enabled, consumes zoom-in/out keys only while the configured modifier is
// held (prevents character leak with Shift modifier); when they are disabled it
// consumes nothing. All other keyboard events pass through. (AC-2.1.18, AC-2.8.14)
// Phase 4: Tracks Ctrl+Alt for temporary toggle (AC-2.7.01–AC-2.7.10).

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

    // Track Win key state via WinKeyManager (AC-2.1.04: both LWin and RWin).
    // Only when Win IS the modifier: with Shift/Alt there is no Start Menu to
    // suppress, so the state machine stays Idle and never injects (AC-2.1.20).
    if (isWinModifier() && (info->vkCode == VK_LWIN || info->vkCode == VK_RWIN))
    {
        if (isDown)
        {
            s_winKeyMgr.onWinKeyDown();
        }
        else if (isUp)
        {
            s_winKeyMgr.onWinKeyUp();
        }
    }

    // Track non-Win modifier state for scroll gesture detection (Issue 1 fix)
    if (!isWinModifier() && isModifierMatch(static_cast<int>(info->vkCode), s_modifierKeyVK))
    {
#ifdef SMOOTHZOOM_INPUT_DIAG  // opt-in per-event hook tracing — deliberately NOT enabled by Debug/SMOOTHZOOM_LOGGING (R-05: hook callbacks must do no I/O)
        {
            wchar_t dbg[128];
            swprintf_s(dbg, L"[SZ-DIAG] ModifierKey vk=0x%X isDown=%d\n",
                info->vkCode, isDown ? 1 : 0);
            OutputDebugStringW(dbg);
        }
#endif
        s_nonWinModifierHeld = isDown;
    }

    // Phase 4/5B: Configurable toggle key tracking (AC-2.7.01–AC-2.7.10)
    bool isToggle1 = isModifierMatch(info->vkCode, s_toggleKey1VK);
    bool isToggle2 = isModifierMatch(info->vkCode, s_toggleKey2VK);

    if (isToggle1) s_toggleKey1Held = isDown;
    if (isToggle2) s_toggleKey2Held = isDown;

    // Held-key tracking above always runs; only the engage/release edge is
    // gated by the "Keyboard shortcuts" setting (see KeyDecision.h).
    switch (decideToggleChord(s_toggleKey1Held && s_toggleKey2Held, s_toggleEngaged, s_shortcutsEnabled))
    {
    case ToggleEdge::Engage:
        s_toggleEngaged = true;
        s_state->commandQueue.push(ZoomCommand::ToggleEngage);
        break;
    case ToggleEdge::Release:
        s_toggleEngaged = false;
        s_state->commandQueue.push(ZoomCommand::ToggleRelease);
        break;
    case ToggleEdge::None:
        break;
    }

    // Clear the inversion-chord edge filter on 'I' release (see s_invertChordDown)
    if (info->vkCode == 'I' && isUp)
        s_invertChordDown = false;

    // Modifier+key zoom shortcuts (Phase 2: AC-2.8.01–AC-2.8.10, Phase 5B: AC-2.1.19).
    // Uses the configurable modifier — not hardcoded to Win. See KeyDecision.h.
    const KeyDecision kd = decideShortcutKey(
        static_cast<int>(info->vkCode), isDown, isUp,
        (info->flags & LLKHF_INJECTED) != 0,
        s_modifierKeyVK, s_shortcutsEnabled && isConfiguredModifierHeld(), s_shortcutsEnabled);

    switch (kd.action)
    {
    case ShortcutAction::ZoomIn:    s_state->commandQueue.push(ZoomCommand::ZoomIn);    break;
    case ShortcutAction::ZoomOut:   s_state->commandQueue.push(ZoomCommand::ZoomOut);   break;
    case ShortcutAction::ResetZoom: s_state->commandQueue.push(ZoomCommand::ResetZoom); break;
    case ShortcutAction::None:      break;
    }
    if (kd.markWinUsedForZoom)
        s_winKeyMgr.markUsedForZoom();

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

        // AC-2.1.18: a non-SmoothZoom key pressed while Win is held (e.g. Win+E/D/L/R,
        // a Windows shell shortcut) must NOT trigger Start-Menu suppression on Win
        // release — the injected Ctrl would leak into the launched app and the shell
        // shortcut should run normally. SmoothZoom's own Win-chord keys (zoom +/-,
        // Esc reset, Ctrl+M settings) still suppress, so they are excluded here.
        if (s_winKeyMgr.state() != WinKeyManager::State::Idle
            && info->vkCode != VK_LWIN && info->vkCode != VK_RWIN
            && !isModifierVK(static_cast<int>(info->vkCode))
            && info->vkCode != VK_OEM_PLUS && info->vkCode != VK_ADD
            && info->vkCode != VK_OEM_MINUS && info->vkCode != VK_SUBTRACT
            && info->vkCode != VK_ESCAPE && info->vkCode != 'M')
        {
            s_winKeyMgr.markUsedWithOtherKey();
        }

        // Win+Ctrl+M → open settings (AC-2.8.11)
        // Always Win-based regardless of configured modifier, and not gated by
        // the "Keyboard shortcuts" setting (it is not a zoom shortcut and types
        // no character). The physical Win check is authoritative; with the Win
        // modifier the state machine must also agree, which guards against a
        // stale state (missed key-up across the secure desktop) turning plain
        // Ctrl+M into this. WinKeyManager is only fed with the Win modifier.
        if (info->vkCode == 'M'
            && (!isWinModifier() || s_winKeyMgr.state() != WinKeyManager::State::Idle)
            && ((GetAsyncKeyState(VK_LWIN) | GetAsyncKeyState(VK_RWIN)) & 0x8000)
            && (GetAsyncKeyState(VK_CONTROL) & 0x8000)
            && s_msgWindow)
        {
            PostMessageW(s_msgWindow, WM_OPEN_SETTINGS, 0, 0);
            if (isWinModifier()) s_winKeyMgr.markUsedForZoom();
        }

        // Ctrl+Alt+I → toggle color inversion (Phase 6: AC-2.10.01), gated by
        // the "Keyboard shortcuts" setting. Edge-triggered via s_invertChordDown
        // — see declaration comment.
        if (info->vkCode == 'I' && s_shortcutsEnabled)
        {
            bool ctrlHeld = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
            bool altHeld  = (GetAsyncKeyState(VK_MENU)    & 0x8000) != 0;
            if (decideInvertChord(static_cast<int>(info->vkCode), isDown, ctrlHeld, altHeld,
                                  s_invertChordDown, s_shortcutsEnabled))
            {
                s_invertChordDown = true;
                s_state->commandQueue.push(ZoomCommand::ToggleInvert);
            }
        }

        // No keyboard exit shortcut. A bare global Ctrl+Q hijacked a near-
        // universal application shortcut (Quit) and silently terminated
        // SmoothZoom from any focused app. The tray-menu "Exit" item is the sole
        // exit mechanism (AC-2.9.16); no AC mandates a keyboard exit.
    }

    // Consume the zoom-in/zoom-out keys (decided above) on BOTH key-down and
    // key-up while the configured modifier is held. Required for non-Win
    // modifiers (e.g., Shift) where Shift+= produces '+' and Shift+- produces '_',
    // leaking into focused text fields, and for peripheral macros (Logitech
    // Options+, AHK, etc.) that send Shift+=/Shift+- via SendInput with
    // LLKHF_INJECTED set — those are consumed too (no injected guard for these
    // four keys; see KeyDecision.h).
    //
    // Why only zoom-in/out (not Esc/settings/toggle/inversion):
    //   - VK_ESCAPE produces no character — safe to pass through (apps may need it).
    //   - Win+Ctrl+M, Ctrl+Alt+I, Ctrl+Alt toggle — all use Ctrl/Alt/Win
    //     which never produce printable characters. Consuming them could break
    //     standard app behavior (e.g., Esc closing dialogs).
    if (kd.consume)
        return 1;   // Consume — prevent character insertion

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
    if (s_stickySuppressed && restoreStickyShortcut(true))
        SZ_LOG_INFO("InputInterceptor", L"Sticky Keys shortcut restored");

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

bool InputInterceptor::crashRestoreStickyKeys()
{
    // Exception-filter safe: two SystemParametersInfo calls on POD state, no
    // registry read, no allocation, no logging.
    return restoreStickyShortcut(false);
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

#ifdef SMOOTHZOOM_DEV_TOOLS
void InputInterceptor::setWheelTap(WheelTapFn fn)
{
    s_wheelTap = fn;
}

bool InputInterceptor::devInjectWheel(WheelKind kind, int16_t rawDelta, bool modifierHeld)
{
    if (s_state == nullptr)
        return false;
    // Same decision and side effects as mouseHookProc; only the modifier verdict
    // comes from the trace instead of live key tracking.
    const bool candidate = wheelIsZoomCandidate(kind, s_modifierKeyVK);
    const WheelDecision d = decideWheel(kind, rawDelta, s_modifierKeyVK, candidate && modifierHeld);
    if (d.consume)
        applyWheelDecision(d);
    return d.consume;
}

int InputInterceptor::configuredModifierVK()
{
    return s_modifierKeyVK;
}
#endif

} // namespace SmoothZoom

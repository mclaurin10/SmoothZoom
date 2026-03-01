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

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <windows.h>

namespace SmoothZoom
{

// Static state — hook callbacks are static C functions, can't use `this`.
static SharedState* s_state = nullptr;
static HHOOK s_mouseHook = nullptr;
static HHOOK s_keyboardHook = nullptr;
static WinKeyManager s_winKeyMgr;

// Phase 5B: Configurable keys — updated by settings observer on main thread.
// Safe to read from hook callbacks (same thread). (AC-2.1.19, AC-2.1.20)
static int s_modifierKeyVK = VK_LWIN;
static int s_toggleKey1VK  = VK_LCONTROL;
static int s_toggleKey2VK  = VK_LMENU;
static HWND s_msgWindow    = nullptr;

// Phase 5B: Settings observer callback — runs on main thread (same as hooks).
static void onSettingsChanged(const SettingsSnapshot& s, void* /*userData*/)
{
    s_modifierKeyVK = s.modifierKeyVK;
    s_toggleKey1VK  = s.toggleKey1VK;
    s_toggleKey2VK  = s.toggleKey2VK;
}

// WM_OPEN_SETTINGS defined in common/AppMessages.h

// ─── Mouse Hook Callback ────────────────────────────────────────────────────
// Minimal: read event, check modifier via WinKeyManager, update atomics, return.
static LRESULT CALLBACK mouseHookProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode < 0 || s_state == nullptr)
        return CallNextHookEx(s_mouseHook, nCode, wParam, lParam);

    auto* info = reinterpret_cast<MSLLHOOKSTRUCT*>(lParam);

    switch (wParam)
    {
    case WM_MOUSEWHEEL:
    {
        // Phase 5B: Configurable modifier key (AC-2.1.19, AC-2.1.20)
        bool modifierHeld = false;
        if (s_modifierKeyVK == VK_LWIN || s_modifierKeyVK == VK_RWIN)
            modifierHeld = s_winKeyMgr.state() != WinKeyManager::State::Idle;
        else
            modifierHeld = (GetAsyncKeyState(toGenericVK(s_modifierKeyVK)) & 0x8000) != 0;

        if (modifierHeld)
        {
            // Extract scroll delta from high word of mouseData
            int16_t delta = static_cast<int16_t>(HIWORD(info->mouseData));

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

    case WM_MOUSEMOVE:
    {
        // Update pointer position for viewport tracking
        s_state->pointerX.store(info->pt.x, std::memory_order_relaxed);
        s_state->pointerY.store(info->pt.y, std::memory_order_relaxed);
        break;
    }

    default:
        break;
    }

    // Pass through to next hook
    return CallNextHookEx(s_mouseHook, nCode, wParam, lParam);
}

// ─── Keyboard Hook Callback ─────────────────────────────────────────────────
// Tracks Win key via WinKeyManager. Observe-only (never consumes). (AC-2.1.18)
// Phase 4: Tracks Ctrl+Alt for temporary toggle (AC-2.7.01–AC-2.7.10).

// Phase 4/5B: Toggle key state — configurable keys (3 bools + 1 queue push), R-05 safe.
static bool s_toggleKey1Held = false;
static bool s_toggleKey2Held = false;
static bool s_toggleEngaged = false;

static LRESULT CALLBACK keyboardHookProc(int nCode, WPARAM wParam, LPARAM lParam)
{
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
            // WinKeyManager injects Ctrl if used for zoom (Start Menu suppression)
            s_winKeyMgr.onWinKeyUp();
            s_state->modifierHeld.store(false, std::memory_order_relaxed);
        }
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

    if (isDown)
    {
        // Record keyboard activity timestamp (for caret tracking priority — Phase 3)
        s_state->lastKeyboardInputTime.store(
            static_cast<int64_t>(info->time), std::memory_order_relaxed);

        // Win+key shortcuts (Phase 2: AC-2.8.01 through AC-2.8.10)
        bool winHeld = s_winKeyMgr.state() != WinKeyManager::State::Idle;
        if (winHeld)
        {
            switch (info->vkCode)
            {
            case VK_OEM_PLUS:   // '=' / '+' on main keyboard
            case VK_ADD:        // '+' on numpad
                s_state->commandQueue.push(ZoomCommand::ZoomIn);
                s_winKeyMgr.markUsedForZoom();
                break;

            case VK_OEM_MINUS:  // '-' on main keyboard
            case VK_SUBTRACT:   // '-' on numpad
                s_state->commandQueue.push(ZoomCommand::ZoomOut);
                s_winKeyMgr.markUsedForZoom();
                break;

            case VK_ESCAPE:
                s_state->commandQueue.push(ZoomCommand::ResetZoom);
                s_winKeyMgr.markUsedForZoom();
                break;

            // Phase 5B: Win+Ctrl+M → open settings (AC-2.8.11)
            case 'M':
                if ((GetAsyncKeyState(VK_CONTROL) & 0x8000) && s_msgWindow)
                {
                    PostMessageW(s_msgWindow, WM_OPEN_SETTINGS, 0, 0);
                    s_winKeyMgr.markUsedForZoom();
                }
                break;
            }
        }

        // Ctrl+Alt+I → toggle color inversion (Phase 6: AC-2.10.01)
        if (info->vkCode == 'I')
        {
            bool ctrlHeld = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
            bool altHeld  = (GetAsyncKeyState(VK_MENU)    & 0x8000) != 0;
            if (ctrlHeld && altHeld)
            {
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

    // Never consume keyboard events (Doc 3 §3.1) — only observe (AC-2.1.18)
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

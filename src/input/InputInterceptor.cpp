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
// =============================================================================

#include "smoothzoom/input/InputInterceptor.h"
#include "smoothzoom/input/WinKeyManager.h"
#include "smoothzoom/common/SharedState.h"
#include "smoothzoom/common/Types.h"

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
        // Check if Win key is held via WinKeyManager state machine (AC-2.1.04)
        bool winHeld = s_winKeyMgr.state() != WinKeyManager::State::Idle;

        if (winHeld)
        {
            // Extract scroll delta from high word of mouseData
            int16_t delta = static_cast<int16_t>(HIWORD(info->mouseData));

            // Atomically accumulate delta (render thread will exchange-with-0)
            s_state->scrollAccumulator.fetch_add(delta, std::memory_order_release);

            // Mark Win key as used for zoom → suppresses Start Menu on release (AC-2.1.16)
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
            }
        }

        // Ctrl+Q → post ResetZoom command and quit
        if (info->vkCode == 'Q')
        {
            bool ctrlHeld = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
            if (ctrlHeld)
            {
                s_state->commandQueue.push(ZoomCommand::ResetZoom);
                PostQuitMessage(0);
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

} // namespace SmoothZoom

// =============================================================================
// SmoothZoom — InputInterceptor
// Global low-level hooks: WH_MOUSE_LL and WH_KEYBOARD_LL. Doc 3 §3.1
//
// CRITICAL: Hook callbacks must be minimal and non-blocking (R-05).
// The system silently deregisters hooks that exceed ~300ms.
// Callbacks: read event → update atomic or post message → return.
// No computation, no I/O, no allocation.
// =============================================================================

#include "smoothzoom/input/InputInterceptor.h"
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

// ─── Mouse Hook Callback ────────────────────────────────────────────────────
// Minimal: read event, check modifier, update atomics, return.
static LRESULT CALLBACK mouseHookProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode < 0 || s_state == nullptr)
        return CallNextHookEx(s_mouseHook, nCode, wParam, lParam);

    auto* info = reinterpret_cast<MSLLHOOKSTRUCT*>(lParam);

    switch (wParam)
    {
    case WM_MOUSEWHEEL:
    {
        // Check if modifier (Win key) is held — Phase 0 uses GetAsyncKeyState
        bool winHeld = (GetAsyncKeyState(VK_LWIN) & 0x8000) != 0 ||
                       (GetAsyncKeyState(VK_RWIN) & 0x8000) != 0;

        if (winHeld)
        {
            // Extract scroll delta from high word of mouseData
            int16_t delta = static_cast<int16_t>(HIWORD(info->mouseData));

            // Atomically accumulate delta (render thread will exchange-with-0)
            s_state->scrollAccumulator.fetch_add(delta, std::memory_order_release);

            // Mark modifier as used for zoom (for Start Menu suppression — Phase 1)
            s_state->modifierHeld.store(true, std::memory_order_relaxed);

            // Consume the event — do not pass to next hook or applications
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
// Phase 0: detect Ctrl+Q for clean exit. Observe-only (never consumes).
static LRESULT CALLBACK keyboardHookProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode < 0 || s_state == nullptr)
        return CallNextHookEx(s_keyboardHook, nCode, wParam, lParam);

    auto* info = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);

    if (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN)
    {
        // Record keyboard activity timestamp (for caret tracking priority — Phase 3)
        s_state->lastKeyboardInputTime.store(
            static_cast<int64_t>(info->time), std::memory_order_relaxed);

        // Phase 0: Ctrl+Q → post ResetZoom command
        if (info->vkCode == 'Q')
        {
            bool ctrlHeld = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
            if (ctrlHeld)
            {
                s_state->commandQueue.push(ZoomCommand::ResetZoom);
                // Also post WM_QUIT to main thread message pump
                PostQuitMessage(0);
            }
        }
    }

    // Never consume keyboard events (Doc 3 §3.1) — only observe
    return CallNextHookEx(s_keyboardHook, nCode, wParam, lParam);
}

// ─── Public Interface ───────────────────────────────────────────────────────

bool InputInterceptor::install(SharedState& state)
{
    s_state = &state;

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

} // namespace SmoothZoom

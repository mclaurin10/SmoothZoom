// =============================================================================
// SmoothZoom — WinKeyManager
// Win key state machine and Start Menu suppression. Doc 3 §3.2
//
// Start Menu suppression (AC-2.1.16): When Win is used for zoom, inject a
// dummy Ctrl keystroke before Win key-up propagates. This prevents Windows
// from interpreting the Win key release as a Start Menu trigger.
// =============================================================================

#include "smoothzoom/input/WinKeyManager.h"

#ifndef SMOOTHZOOM_TESTING
#include <Windows.h>
#endif

namespace SmoothZoom
{

void WinKeyManager::onWinKeyDown()
{
    if (state_ == State::Idle)
        state_ = State::HeldClean;
}

void WinKeyManager::onWinKeyUp()
{
    if (state_ == State::HeldUsed)
    {
        // Suppress Start Menu by injecting Ctrl press+release (AC-2.1.16).
        // Windows tracks whether Win was used in a chord; a Ctrl keystroke
        // makes it look like Win+Ctrl was pressed, preventing the Start Menu.
#ifndef SMOOTHZOOM_TESTING
        INPUT inputs[2] = {};

        // Ctrl down
        inputs[0].type = INPUT_KEYBOARD;
        inputs[0].ki.wVk = VK_CONTROL;

        // Ctrl up
        inputs[1].type = INPUT_KEYBOARD;
        inputs[1].ki.wVk = VK_CONTROL;
        inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;

        SendInput(2, inputs, sizeof(INPUT));
#endif
    }

    state_ = State::Idle;
}

void WinKeyManager::markUsedForZoom()
{
    if (state_ == State::HeldClean)
        state_ = State::HeldUsed;
}

} // namespace SmoothZoom

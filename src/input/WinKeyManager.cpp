// =============================================================================
// SmoothZoom — WinKeyManager
// Win key state machine and Start Menu suppression. Doc 3 §3.2
//
// Phase 0: State machine implemented but suppression not active.
// Phase 1: Full Start Menu suppression via SendInput Ctrl injection.
// =============================================================================

#include "smoothzoom/input/WinKeyManager.h"

namespace SmoothZoom
{

void WinKeyManager::onWinKeyDown()
{
    if (state_ == State::Idle)
        state_ = State::HeldClean;
}

void WinKeyManager::onWinKeyUp()
{
    // Phase 1 will add: if state_ == HeldUsed → inject Ctrl to suppress Start Menu
    state_ = State::Idle;
}

void WinKeyManager::markUsedForZoom()
{
    if (state_ == State::HeldClean)
        state_ = State::HeldUsed;
}

} // namespace SmoothZoom

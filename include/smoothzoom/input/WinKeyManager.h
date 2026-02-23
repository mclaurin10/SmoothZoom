#pragma once
// =============================================================================
// SmoothZoom — WinKeyManager
// Win key state machine and Start Menu suppression. Doc 3 §3.2
// =============================================================================

namespace SmoothZoom
{

class WinKeyManager
{
public:
    enum class State
    {
        Idle,       // Win not pressed
        HeldClean,  // Win pressed, no zoom action yet
        HeldUsed,   // Win pressed and used for zoom (suppress Start Menu on release)
    };

    void onWinKeyDown();
    void onWinKeyUp();
    void markUsedForZoom();

    State state() const { return state_; }
    bool shouldSuppressStartMenu() const { return state_ == State::HeldUsed; }

private:
    State state_ = State::Idle;
};

} // namespace SmoothZoom

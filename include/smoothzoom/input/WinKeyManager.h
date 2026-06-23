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
    // AC-2.1.18: the Win key was pressed in a non-SmoothZoom chord (e.g. Win+E/D/L).
    // In that case onWinKeyUp() must NOT inject the Start-Menu-suppression Ctrl, or a
    // stray Ctrl leaks into the launched app and the shell shortcut is disrupted.
    void markUsedWithOtherKey();

    void reset() { state_ = State::Idle; usedWithOtherKey_ = false; }

    State state() const { return state_; }
    // Suppress only when Win was used for zoom AND not also used in another chord.
    bool shouldSuppressStartMenu() const
    {
        return state_ == State::HeldUsed && !usedWithOtherKey_;
    }

private:
    State state_ = State::Idle;
    bool usedWithOtherKey_ = false;
};

} // namespace SmoothZoom

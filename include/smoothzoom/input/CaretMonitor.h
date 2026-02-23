#pragma once
// =============================================================================
// SmoothZoom — CaretMonitor
// UIA text-pattern caret + GTTI polling fallback. Runs on UIA thread. Doc 3 §3.6
// Phase 3 component.
//
// Two techniques for caret tracking:
// 1. UIA TextPattern — preferred, event-driven, works in modern apps
// 2. GetGUIThreadInfo — polling fallback at 30Hz, works in Notepad/Terminal
//
// Writes caret rectangle to SharedState::caretRect via SeqLock.
// Silent degradation when caret unavailable (AC-2.6.11).
// =============================================================================

#include <atomic>

namespace SmoothZoom
{

struct SharedState;

class CaretMonitor
{
public:
    ~CaretMonitor();

    // Start caret monitoring. Called after FocusMonitor is running.
    // Returns false if initialization fails (non-fatal per AC-2.6.11).
    bool start(SharedState& state);

    // Stop monitoring and clean up.
    void stop();

private:
    struct Impl;
    Impl* impl_ = nullptr;
    std::atomic<bool> running_{false};
};

} // namespace SmoothZoom

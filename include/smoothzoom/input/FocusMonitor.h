#pragma once
// =============================================================================
// SmoothZoom — FocusMonitor
// UIA focus-changed event subscription. Runs on UIA thread. Doc 3 §3.5
// Phase 3 component.
//
// Subscribes to IUIAutomation focus-changed events. On each event, extracts the
// focused element's bounding rectangle, validates it, and writes to shared state
// via SeqLock. Debounce logic lives in ViewportTracker, not here (AC-2.5.07).
// =============================================================================

#include <atomic>

namespace SmoothZoom
{

struct SharedState;

class FocusMonitor
{
public:
    ~FocusMonitor();

    // Start monitoring on the UIA thread. Returns false if UIA init fails.
    // Graceful degradation: failure is non-fatal (AC-2.5.14).
    bool start(SharedState& state);

    // Stop monitoring and clean up UIA resources.
    void stop();

private:
    struct Impl;
    Impl* impl_ = nullptr;
    std::atomic<bool> running_{false};
};

} // namespace SmoothZoom

#pragma once
// =============================================================================
// SmoothZoom — FocusMonitor
// UIA focus-changed event subscription. Runs on UIA thread. Doc 3 §3.3
// Phase 3 component.
// =============================================================================

namespace SmoothZoom
{

struct SharedState;

class FocusMonitor
{
public:
    bool start(SharedState& state);
    void stop();
};

} // namespace SmoothZoom

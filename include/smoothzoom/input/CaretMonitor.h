#pragma once
// =============================================================================
// SmoothZoom — CaretMonitor
// UIA text-pattern caret + GTTI polling fallback. Runs on UIA thread. Doc 3 §3.4
// Phase 3 component.
// =============================================================================

namespace SmoothZoom
{

struct SharedState;

class CaretMonitor
{
public:
    bool start(SharedState& state);
    void stop();
};

} // namespace SmoothZoom

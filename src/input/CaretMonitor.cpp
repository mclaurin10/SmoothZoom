// =============================================================================
// SmoothZoom — CaretMonitor (STUB)
// UIA text-pattern + GTTI polling fallback. Doc 3 §3.6
// Phase 3 component — not implemented until then.
// =============================================================================

#include "smoothzoom/input/CaretMonitor.h"

namespace SmoothZoom
{

bool CaretMonitor::start(SharedState& /*state*/)
{
    // Phase 3: TextPattern subscription + GetGUIThreadInfo polling at 30Hz
    return true;
}

void CaretMonitor::stop()
{
    // Phase 3: Unsubscribe, stop polling
}

} // namespace SmoothZoom

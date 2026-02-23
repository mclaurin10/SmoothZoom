// =============================================================================
// SmoothZoom — FocusMonitor (STUB)
// UIA focus-changed event subscription. Doc 3 §3.5
// Phase 3 component — not implemented until then.
// =============================================================================

#include "smoothzoom/input/FocusMonitor.h"

namespace SmoothZoom
{

bool FocusMonitor::start(SharedState& /*state*/)
{
    // Phase 3: UIA thread, CoInitializeEx, AddFocusChangedEventHandler
    return true;
}

void FocusMonitor::stop()
{
    // Phase 3: RemoveFocusChangedEventHandler, CoUninitialize
}

} // namespace SmoothZoom

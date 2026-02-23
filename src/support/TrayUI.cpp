// =============================================================================
// SmoothZoom — TrayUI (STUB)
// System tray icon, context menu, settings window. Doc 3 §3.10
// Phase 5 component — not implemented until then.
// =============================================================================

#include "smoothzoom/support/TrayUI.h"

namespace SmoothZoom
{

bool TrayUI::create()
{
    // Phase 5: Shell_NotifyIcon, context menu, settings dialog
    return true;
}

void TrayUI::destroy()
{
    // Phase 5: Shell_NotifyIcon delete
}

void TrayUI::showSettingsWindow()
{
    // Phase 5: Show/create settings dialog
}

} // namespace SmoothZoom

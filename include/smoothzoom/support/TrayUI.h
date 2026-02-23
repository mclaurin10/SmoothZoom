#pragma once
// =============================================================================
// SmoothZoom — TrayUI
// System tray icon, context menu, settings window. Doc 3 §3.10
// Phase 5 component.
// =============================================================================

namespace SmoothZoom
{

class TrayUI
{
public:
    bool create();
    void destroy();
    void showSettingsWindow();
};

} // namespace SmoothZoom

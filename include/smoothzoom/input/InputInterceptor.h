#pragma once
// =============================================================================
// SmoothZoom — InputInterceptor
// Installs WH_MOUSE_LL and WH_KEYBOARD_LL hooks, routes events.
// Doc 3 §3.1
// =============================================================================

namespace SmoothZoom
{

struct SharedState;
class SettingsManager;

class InputInterceptor
{
public:
    bool install(SharedState& state);
    void uninstall();
    bool isHealthy() const;

    // Reinstall deregistered hooks (R-05 watchdog, AC-ERR.03)
    bool reinstall();

    // Phase 5B: Register for settings change notifications (AC-2.9.04).
    // Callback updates configurable modifier/toggle keys on main thread.
    static void registerSettingsObserver(SettingsManager& mgr);

    // Phase 5B: Store message window handle for Win+Ctrl+M posting (AC-2.8.11).
    // Uses void* to avoid pulling in windows.h in the header.
    static void setMessageWindow(void* hWnd);
};

} // namespace SmoothZoom

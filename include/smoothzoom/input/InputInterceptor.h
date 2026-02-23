#pragma once
// =============================================================================
// SmoothZoom — InputInterceptor
// Installs WH_MOUSE_LL and WH_KEYBOARD_LL hooks, routes events.
// Doc 3 §3.1
// =============================================================================

namespace SmoothZoom
{

struct SharedState;

class InputInterceptor
{
public:
    bool install(SharedState& state);
    void uninstall();
    bool isHealthy() const;

    // Reinstall deregistered hooks (R-05 watchdog, AC-ERR.03)
    bool reinstall();
};

} // namespace SmoothZoom

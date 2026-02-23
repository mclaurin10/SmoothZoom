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

private:
    // Hook handles — platform-specific (HHOOK on Win32)
};

} // namespace SmoothZoom

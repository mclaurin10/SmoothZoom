#pragma once
// =============================================================================
// SmoothZoom — InputInterceptor
// Installs WH_MOUSE_LL and WH_KEYBOARD_LL hooks, routes events.
// Doc 3 §3.1
// =============================================================================

#include <cstdint>

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

    // Unconditional unhook + rehook for the R-05 liveness watchdog. Silent OS
    // deregistration leaves the HHOOK non-null, so isHealthy()/reinstall()
    // cannot recover from it. Also resets transient key state (key-ups were
    // likely missed during the outage).
    bool forceReinstall();

    // GetTickCount64 timestamp of the most recent hook callback invocation.
    // The watchdog compares this against GetLastInputInfo to detect dead hooks.
    static int64_t lastCallbackTick();

    // Clear all held/engaged key state derived from hook events (Win key state
    // machine, modifier/toggle flags). Call when key-ups may have been missed:
    // secure-desktop lock/unlock (AC-ERR.04) and hook reinstall.
    static void resetTransientKeyState();

    // Phase 5B: Register for settings change notifications (AC-2.9.04).
    // Callback updates configurable modifier/toggle keys on main thread.
    static void registerSettingsObserver(SettingsManager& mgr);

    // Phase 5B: Store message window handle for Win+Ctrl+M posting (AC-2.8.11).
    // Uses void* to avoid pulling in windows.h in the header.
    static void setMessageWindow(void* hWnd);
};

} // namespace SmoothZoom

#pragma once
// =============================================================================
// SmoothZoom — RenderLoop
// Dedicated render thread: frame tick, interpolation, calls MagBridge.
// Synchronized to VSync via DwmFlush(). Doc 3 §3.7
// No heap allocation, no mutex acquisition on the hot path.
// =============================================================================

#include <atomic>

namespace SmoothZoom
{

struct SharedState;
class ZoomController;
class ViewportTracker;

class RenderLoop
{
public:
    void start(SharedState& state);
    void requestShutdown();
    bool isRunning() const;

    // Post-init conflict detection (AC-ERR.02 / AC-ERR.01). The render thread
    // reads the system's full-screen transform once, right after MagInitialize.
    // A non-1.0× transform means another magnifier (or a leftover transform) is
    // active. Surfaced here for the main thread to warn the user — the readback
    // itself must stay on the render thread (Mag* affinity, R-01). Valid to read
    // once start() has returned (it spin-waits on initComplete_).
    bool magnifierConflictActive() const;

    // Legacy no-op. MagUninitialize now happens on the render thread.
    void finalizeShutdown();

    // Called by the render thread function — public so the static trampoline
    // in RenderLoop.cpp can invoke it. Not part of the external API.
    void threadMain();

private:
    void frameTick();

    std::atomic<bool> shutdownRequested_{false};
    std::atomic<bool> running_{false};
    std::atomic<bool> initComplete_{false};

    // Set on the render thread (post-init transform readback), read on the main
    // thread after start() returns. Published via the initComplete_ release fence.
    std::atomic<bool> magnifierConflictDetected_{false};
};

} // namespace SmoothZoom

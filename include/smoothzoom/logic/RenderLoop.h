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
};

} // namespace SmoothZoom

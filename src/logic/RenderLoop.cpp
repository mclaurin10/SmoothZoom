// =============================================================================
// SmoothZoom — RenderLoop
// Dedicated render thread: frame tick, VSync sync via DwmFlush(). Doc 3 §3.7
//
// HOT PATH INVARIANTS (rules/render-loop.md):
//   1. No heap allocation inside frameTick()
//   2. No mutex acquisition (atomics + SeqLock only)
//   3. No I/O on hot path
//   4. No blocking calls other than DwmFlush()
// =============================================================================

#include "smoothzoom/logic/RenderLoop.h"
#include "smoothzoom/common/SharedState.h"
#include "smoothzoom/logic/ZoomController.h"
#include "smoothzoom/logic/ViewportTracker.h"
#include "smoothzoom/output/MagBridge.h"

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <windows.h>
#include <dwmapi.h>
#include <thread>

#pragma comment(lib, "Dwmapi.lib")

namespace SmoothZoom
{

// These are owned by the render thread — no concurrent access.
// Declared at file scope so frameTick() doesn't allocate.
static SharedState* s_state = nullptr;
static MagBridge s_magBridge;
static ZoomController s_zoomController;
static ViewportTracker s_viewportTracker;

// Last-frame values for redundant-call optimization
static float s_lastZoom = 1.0f;
static float s_lastOffX = 0.0f;
static float s_lastOffY = 0.0f;

// Screen dimensions (cached once at start, Phase 6 updates on display change)
static int s_screenW = 0;
static int s_screenH = 0;

// Deadzone filter for pointer micro-jitter suppression (AC-2.4.09–AC-2.4.11).
// Tracks last "committed" pointer position. Viewport only updates when pointer
// moves beyond the deadzone threshold, preventing visible jitter from
// sub-pixel pointer noise while remaining imperceptible during intentional movement.
static int32_t s_committedPtrX = 0;
static int32_t s_committedPtrY = 0;
static bool s_deadzoneInitialized = false;

// Forward declare the thread trampoline
static void renderThreadMain(RenderLoop* self);

void RenderLoop::start(SharedState& state)
{
    if (running_.load(std::memory_order_relaxed))
        return;

    s_state = &state;
    shutdownRequested_.store(false, std::memory_order_relaxed);

    // Cache screen dimensions (render thread will use these)
    s_screenW = GetSystemMetrics(SM_CXSCREEN);
    s_screenH = GetSystemMetrics(SM_CYSCREEN);

    // Initialize MagBridge on the main thread (required by API)
    if (!s_magBridge.initialize())
        return;

    running_.store(true, std::memory_order_release);

    // Launch render thread
    std::thread renderThread(renderThreadMain, this);
    renderThread.detach();
}

void RenderLoop::requestShutdown()
{
    shutdownRequested_.store(true, std::memory_order_release);
}

bool RenderLoop::isRunning() const
{
    return running_.load(std::memory_order_acquire);
}

void RenderLoop::threadMain()
{
    // Frame pacing loop (Doc 3 §3.7):
    //   frameTick() → DwmFlush() → repeat
    while (!shutdownRequested_.load(std::memory_order_acquire))
    {
        frameTick();
        DwmFlush(); // Block until next VSync
    }

    // Reset zoom to 1.0× on the render thread (these API calls are render-thread-safe)
    s_magBridge.setTransform(1.0f, 0.0f, 0.0f);
    s_magBridge.setInputTransform(1.0f, 0.0f, 0.0f);

    // Note: MagUninitialize() must be called from the main thread.
    // The main thread calls finalizeShutdown() after detecting !isRunning().

    running_.store(false, std::memory_order_release);
}

void RenderLoop::finalizeShutdown()
{
    // Called from main thread after render thread has stopped.
    // MagUninitialize must be called from the main thread.
    s_magBridge.shutdown();
}

static void renderThreadMain(RenderLoop* self)
{
    self->threadMain();
}

// =============================================================================
// frameTick — the hot path. No heap alloc, no mutex, no I/O.
// =============================================================================
void RenderLoop::frameTick()
{
    // 1. Consume scroll delta (atomic exchange with 0)
    int32_t scrollDelta = s_state->scrollAccumulator.exchange(0, std::memory_order_acquire);

    // 2. Drain keyboard commands (lock-free queue)
    // Phase 2+ will process ZoomIn/ZoomOut/ResetZoom commands here.
    // For Phase 0, we only check for ResetZoom (Ctrl+Q).
    while (auto cmd = s_state->commandQueue.pop())
    {
        if (*cmd == ZoomCommand::ResetZoom)
        {
            s_zoomController.reset();
        }
        // Phase 2+: handle ZoomIn, ZoomOut, ToggleEngage, etc.
    }

    // 3. Apply scroll delta to zoom (if any)
    if (scrollDelta != 0)
    {
        s_zoomController.applyScrollDelta(scrollDelta);
    }

    // 4. Advance animation (Phase 2+ — currently a no-op for scroll-direct)
    s_zoomController.tick(0.0f);

    // Get current zoom level
    float zoom = s_zoomController.currentZoom();

    // 5. Compute viewport offset with deadzone filtering (AC-2.4.09–AC-2.4.11)
    int32_t rawPtrX = s_state->pointerX.load(std::memory_order_relaxed);
    int32_t rawPtrY = s_state->pointerY.load(std::memory_order_relaxed);

    // Deadzone: suppress viewport updates for micro-movements (3px at 1080p, scales with resolution)
    int32_t deadzoneThreshold = s_screenH > 0 ? (3 * s_screenH / 1080) : 3;
    if (deadzoneThreshold < 1) deadzoneThreshold = 1;

    if (!s_deadzoneInitialized)
    {
        s_committedPtrX = rawPtrX;
        s_committedPtrY = rawPtrY;
        s_deadzoneInitialized = true;
    }

    int32_t dx = rawPtrX - s_committedPtrX;
    int32_t dy = rawPtrY - s_committedPtrY;
    if (dx * dx + dy * dy > deadzoneThreshold * deadzoneThreshold)
    {
        // Movement exceeds deadzone — commit the new position
        s_committedPtrX = rawPtrX;
        s_committedPtrY = rawPtrY;
    }

    auto offset = ViewportTracker::computePointerOffset(
        s_committedPtrX, s_committedPtrY, zoom, s_screenW, s_screenH);

    // 6 + 7. Apply to MagBridge — only if values changed since last frame.
    // Both calls use identical zoom/offset values (R-04: same frame, same block).
    bool changed = (zoom != s_lastZoom || offset.x != s_lastOffX || offset.y != s_lastOffY);

    if (changed)
    {
        s_magBridge.setTransform(zoom, offset.x, offset.y);
        s_magBridge.setInputTransform(zoom, offset.x, offset.y);

        s_lastZoom = zoom;
        s_lastOffX = offset.x;
        s_lastOffY = offset.y;
    }
}

} // namespace SmoothZoom

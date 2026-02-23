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
#include <chrono>

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

// Frame timing for dt computation (QPC is a hardware register read — safe for hot path)
static int64_t s_lastFrameTimeQpc = 0;
static int64_t s_qpcFrequency = 0;

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

// Phase 3: Source tracking state — all pre-allocated, no heap on hot path.
static int64_t s_lastPointerMoveTimeMs = 0;  // Updated when committed pointer changes
static TrackingSource s_activeSource = TrackingSource::Pointer;

// Source transition smoothing (200ms ease-out between sources)
static constexpr float kSourceTransitionDurationMs = 200.0f;
static float s_transitionOffX = 0.0f;   // Starting offset when transition began
static float s_transitionOffY = 0.0f;
static float s_transitionElapsedMs = 0.0f;
static bool s_sourceTransitionActive = false;

// Get monotonic time in milliseconds (for source priority timestamps)
static int64_t currentTimeMs()
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

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

    // Initialize frame timing for dt computation
    LARGE_INTEGER freq, now;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&now);
    s_qpcFrequency = freq.QuadPart;
    s_lastFrameTimeQpc = now.QuadPart;

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
    while (auto cmd = s_state->commandQueue.pop())
    {
        switch (*cmd)
        {
        case ZoomCommand::ZoomIn:
            s_zoomController.applyKeyboardStep(+1);
            break;
        case ZoomCommand::ZoomOut:
            s_zoomController.applyKeyboardStep(-1);
            break;
        case ZoomCommand::ResetZoom:
            s_zoomController.animateToZoom(1.0f);
            break;
        default:
            break; // Phase 4+: ToggleEngage, ToggleRelease
        }
    }

    // 3. Apply scroll delta to zoom (if any)
    if (scrollDelta != 0)
    {
        s_zoomController.applyScrollDelta(scrollDelta);
    }

    // 4. Compute dt and advance animation (Phase 2: ease-out interpolation)
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    float dtSeconds = 0.0f;
    if (s_lastFrameTimeQpc > 0 && s_qpcFrequency > 0)
    {
        dtSeconds = static_cast<float>(
            static_cast<double>(now.QuadPart - s_lastFrameTimeQpc) /
            static_cast<double>(s_qpcFrequency));
        if (dtSeconds > 0.1f) dtSeconds = 0.1f;
        if (dtSeconds < 0.0f) dtSeconds = 0.0f;
    }
    s_lastFrameTimeQpc = now.QuadPart;

    s_zoomController.tick(dtSeconds);

    // Get current zoom level
    float zoom = s_zoomController.currentZoom();

    // 5. Compute viewport offset with multi-source tracking (Phase 3)
    //    Reads are all lock-free (atomics + SeqLock) — no hot path violations.

    // 5a. Pointer position with deadzone filtering (AC-2.4.09–AC-2.4.11)
    int32_t rawPtrX = s_state->pointerX.load(std::memory_order_relaxed);
    int32_t rawPtrY = s_state->pointerY.load(std::memory_order_relaxed);

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
    bool pointerMoved = (dx * dx + dy * dy > deadzoneThreshold * deadzoneThreshold);
    if (pointerMoved)
    {
        s_committedPtrX = rawPtrX;
        s_committedPtrY = rawPtrY;
        s_lastPointerMoveTimeMs = currentTimeMs();
    }

    // 5b. Read timestamps and rects for source priority arbitration
    int64_t nowMs = currentTimeMs();
    int64_t lastFocusChange = s_state->lastFocusChangeTime.load(std::memory_order_acquire);
    int64_t lastKeyboardInput = s_state->lastKeyboardInputTime.load(std::memory_order_acquire);

    // Read focus/caret rects via SeqLock (lock-free reader)
    ScreenRect focusRect = s_state->focusRect.read();
    ScreenRect caretRect = s_state->caretRect.read();

    // Validate rects (non-zero area means valid data has been written)
    bool focusValid = (focusRect.width() > 0 && focusRect.height() > 0);
    bool caretValid = (caretRect.width() >= 0 && caretRect.height() > 0); // Caret can be 0-width

    // 5c. Determine active tracking source
    TrackingSource newSource = s_viewportTracker.determineActiveSource(
        nowMs, s_lastPointerMoveTimeMs, lastFocusChange, lastKeyboardInput,
        focusValid, caretValid);

    // 5d. Compute target offset based on active source
    ViewportTracker::Offset targetOffset;
    switch (newSource)
    {
    case TrackingSource::Caret:
        targetOffset = ViewportTracker::computeCaretOffset(
            caretRect, zoom, s_screenW, s_screenH);
        break;
    case TrackingSource::Focus:
        targetOffset = ViewportTracker::computeElementOffset(
            focusRect, zoom, s_screenW, s_screenH);
        break;
    case TrackingSource::Pointer:
    default:
        targetOffset = ViewportTracker::computePointerOffset(
            s_committedPtrX, s_committedPtrY, zoom, s_screenW, s_screenH);
        break;
    }

    // 5e. Source transition smoothing (200ms ease-out between sources)
    // When the active source changes, don't snap — interpolate from current to new.
    if (newSource != s_activeSource)
    {
        // Begin transition: save current offset as starting point
        s_transitionOffX = s_lastOffX;
        s_transitionOffY = s_lastOffY;
        s_transitionElapsedMs = 0.0f;
        s_sourceTransitionActive = true;
        s_activeSource = newSource;
    }

    ViewportTracker::Offset offset = targetOffset;
    if (s_sourceTransitionActive)
    {
        s_transitionElapsedMs += dtSeconds * 1000.0f;
        float t = s_transitionElapsedMs / kSourceTransitionDurationMs;
        if (t >= 1.0f)
        {
            // Transition complete
            s_sourceTransitionActive = false;
        }
        else
        {
            // Ease-out: 1 - (1-t)^2
            float eased = 1.0f - (1.0f - t) * (1.0f - t);
            offset.x = s_transitionOffX + (targetOffset.x - s_transitionOffX) * eased;
            offset.y = s_transitionOffY + (targetOffset.y - s_transitionOffY) * eased;
        }
    }

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

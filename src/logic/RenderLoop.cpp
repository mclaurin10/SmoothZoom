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

// Screen dimensions — read from SharedState each frame so WM_DISPLAYCHANGE updates apply.
// Relaxed atomic int loads per frame (~1ns each), safe for hot path.
static int s_screenW = 0;
static int s_screenH = 0;

// Virtual screen origin — can be negative with multi-monitor (E6.4–E6.7)
static int s_screenOriginX = 0;
static int s_screenOriginY = 0;

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

// Phase 5B: Settings integration — render thread checks version once per frame.
// Common case cost: one atomic uint64 load + comparison. No heap, no mutex.
static uint64_t s_cachedSettingsVersion = 0;
static bool s_followKeyboardFocus = true;   // Match SettingsSnapshot defaults
static bool s_followTextCursor = true;

// Phase 6: Color inversion state — toggled by ToggleInvert command, synced from settings.
// Render thread only (no contention). (AC-2.10.01–AC-2.10.05)
static bool s_colorInversionActive = false;

// MagBridge error tracking — log on state transitions only (not every frame)
static bool s_magBridgeLastOk = true;

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
    initComplete_.store(false, std::memory_order_relaxed);

    // Initialize frame timing for dt computation
    LARGE_INTEGER freq, now;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&now);
    s_qpcFrequency = freq.QuadPart;
    s_lastFrameTimeQpc = now.QuadPart;

    // Launch render thread — MagBridge init happens there so all Mag* API
    // calls share the same thread (thread affinity requirement).
    std::thread renderThread(renderThreadMain, this);
    renderThread.detach();

    // Spin-wait for render thread to finish MagBridge initialization.
    // Allows main thread to check isRunning() and show error dialog on failure.
    while (!initComplete_.load(std::memory_order_acquire))
    {
        Sleep(1);
    }
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
    // Initialize MagBridge on the render thread so all Mag* API calls
    // (MagInitialize, MagSetFullscreenTransform, MagUninitialize) share
    // the same thread. The Magnification API has undocumented thread
    // affinity — offsets are silently ignored when MagSetFullscreenTransform
    // is called from a different thread than MagInitialize.
    if (!s_magBridge.initialize())
    {
        // Leave running_ false so main thread sees failure via isRunning()
        initComplete_.store(true, std::memory_order_release);
        return;
    }

    running_.store(true, std::memory_order_release);
    initComplete_.store(true, std::memory_order_release);

    // Frame pacing loop (Doc 3 §3.7):
    //   frameTick() → pump messages → DwmFlush() → repeat
    // The PeekMessage pump is required for MagSetFullscreenTransform offsets
    // to take effect. The Magnification API uses internal DWM messages to apply
    // viewport offsets; without a message pump on the calling thread, offsets
    // are silently ignored while the zoom factor still applies.
    while (!shutdownRequested_.load(std::memory_order_acquire))
    {
        frameTick();

        // Pump messages for Magnification API internals.
        // PeekMessage with no pending messages is ~1µs, no heap alloc, no mutex.
        // The render thread has no windows and no hooks, so no unexpected messages.
        MSG msg;
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        DwmFlush(); // Block until next VSync
    }

    // Reset zoom to 1.0× then shut down MagBridge, all on the render thread.
    s_magBridge.setTransform(1.0f, 0.0f, 0.0f);
    s_magBridge.shutdown();

    running_.store(false, std::memory_order_release);
}

void RenderLoop::finalizeShutdown()
{
    // No-op. MagBridge init and shutdown now both happen on the render thread
    // to satisfy Magnification API thread affinity.
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
    // 0. Check for settings changes (Phase 5B: AC-2.9.04–AC-2.9.09)
    //    One atomic uint64 load per frame (common case). shared_ptr load only on change.
    uint64_t ver = s_state->settingsVersion.load(std::memory_order_acquire);
    if (ver != s_cachedSettingsVersion)
    {
        auto snap = std::atomic_load(&s_state->settingsSnapshot);
        if (snap)
        {
            s_zoomController.applySettings(
                snap->minZoom, snap->maxZoom,
                snap->keyboardZoomStep, snap->defaultZoomLevel,
                snap->animationSpeed);
            s_followKeyboardFocus = snap->followKeyboardFocus;
            s_followTextCursor = snap->followTextCursor;

            // Phase 6: Sync color inversion from settings (AC-2.10.03, AC-2.10.04)
            if (snap->colorInversionEnabled != s_colorInversionActive)
            {
                s_colorInversionActive = snap->colorInversionEnabled;
                s_magBridge.setColorInversion(s_colorInversionActive);
            }
        }
        s_cachedSettingsVersion = ver;
    }

    // 0b. Update screen dimensions from shared state (WM_DISPLAYCHANGE → main thread → atomics)
    s_screenW = s_state->screenWidth.load(std::memory_order_relaxed);
    s_screenH = s_state->screenHeight.load(std::memory_order_relaxed);
    s_screenOriginX = s_state->screenOriginX.load(std::memory_order_relaxed);
    s_screenOriginY = s_state->screenOriginY.load(std::memory_order_relaxed);

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
        case ZoomCommand::ToggleEngage:
            s_zoomController.engageToggle();
            break;
        case ZoomCommand::ToggleRelease:
            s_zoomController.releaseToggle();
            break;
        case ZoomCommand::TrayToggle:
            s_zoomController.trayToggle();
            break;
        case ZoomCommand::ToggleInvert:
            // AC-2.10.01: instantaneous toggle, no animation
            s_colorInversionActive = !s_colorInversionActive;
            s_magBridge.setColorInversion(s_colorInversionActive);
            break;
        default:
            break;
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
    // Use GetCursorPos() directly instead of SharedState atomics. The low-level
    // mouse hook's WM_MOUSEMOVE events are not reliably delivered when the
    // fullscreen magnifier is active (DWM handles cursor rendering at a level
    // that bypasses the hook chain). GetCursorPos() is a fast shared-memory
    // read (~1µs), no heap allocation, no mutex — safe for the hot path.
    POINT cursorPos;
    GetCursorPos(&cursorPos);
    int32_t rawPtrX = cursorPos.x;
    int32_t rawPtrY = cursorPos.y;

    // Use primary monitor height for deadzone scaling (s_screenH is now virtual screen height)
    int32_t primaryH = GetSystemMetrics(SM_CYSCREEN);
    int32_t deadzoneThreshold = primaryH > 0 ? (3 * primaryH / 1080) : 3;
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

    // Validate rects: non-zero area + bounds check (defense-in-depth for R-09)
    bool focusValid = (focusRect.width() > 0 && focusRect.height() > 0
        && focusRect.left > -5000 && focusRect.top > -5000
        && focusRect.width() <= 10000 && focusRect.height() <= 10000);
    bool caretValid = (caretRect.width() >= 0 && caretRect.height() > 0 // Caret can be 0-width
        && caretRect.left > -5000 && caretRect.top > -5000
        && caretRect.height() <= 5000);

    // Phase 5B: Gate on settings (AC-2.9.08, AC-2.9.09)
    focusValid = focusValid && s_followKeyboardFocus;
    caretValid = caretValid && s_followTextCursor;

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
            caretRect, zoom, s_screenW, s_screenH, s_screenOriginX, s_screenOriginY);
        break;
    case TrackingSource::Focus:
        targetOffset = ViewportTracker::computeElementOffset(
            focusRect, zoom, s_screenW, s_screenH, s_screenOriginX, s_screenOriginY);
        break;
    case TrackingSource::Pointer:
    default:
        targetOffset = ViewportTracker::computePointerOffset(
            s_committedPtrX, s_committedPtrY, zoom, s_screenW, s_screenH,
            s_screenOriginX, s_screenOriginY);
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

    // 6. Apply to MagBridge — only if values changed since last frame.
    bool changed = (zoom != s_lastZoom || offset.x != s_lastOffX || offset.y != s_lastOffY);

    if (changed)
    {
        bool ok = s_magBridge.setTransform(zoom, offset.x, offset.y);

        // Log on transition: first failure and recovery (not every frame)
        if (!ok && s_magBridgeLastOk)
            OutputDebugStringW(L"SmoothZoom: MagBridge setTransform failed\n");
        else if (ok && !s_magBridgeLastOk)
            OutputDebugStringW(L"SmoothZoom: MagBridge setTransform recovered\n");
        s_magBridgeLastOk = ok;

        s_lastZoom = zoom;
        s_lastOffX = offset.x;
        s_lastOffY = offset.y;

        // Phase 5C: publish for main thread (graceful exit, tray tooltip)
        s_state->currentZoomLevel.store(zoom, std::memory_order_relaxed);
    }
}

} // namespace SmoothZoom

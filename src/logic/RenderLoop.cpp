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
#include "smoothzoom/support/Logger.h"

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
static int64_t s_lastPointerMoveTimeMs = 0;  // Updated on ANY pointer movement (WS2A fix)
static TrackingSource s_activeSource = TrackingSource::Pointer;

// Raw pointer position — tracks any movement for timestamp updates (WS2A).
// Separate from committed position (deadzone-filtered) used for viewport offset.
static int32_t s_lastRawPtrX = 0;
static int32_t s_lastRawPtrY = 0;

// Phase 5B: Settings integration — render thread checks version once per frame.
// Common case cost: one atomic uint64 load + comparison. No heap, no mutex.
static uint64_t s_cachedSettingsVersion = 0;
static bool s_followKeyboardFocus = true;   // Match SettingsSnapshot defaults
static bool s_followTextCursor = true;
static bool s_reverseScrollDirection = false;

// Phase 6: Color inversion state — toggled by ToggleInvert command, synced from settings.
// Render thread only (no contention). (AC-2.10.01–AC-2.10.05)
static bool s_colorInversionActive = false;

// BF-1: Force initial settings application on first tick (AC-2.10.04).
// Without this, color inversion from config.json is not applied on startup because
// s_cachedSettingsVersion and settingsVersion both start at 0 (no version mismatch).
static bool s_firstTick = true;

// MagBridge error tracking — log on state transitions only (not every frame)
static bool s_magBridgeLastOk = true;

#ifdef SMOOTHZOOM_PERF_AUDIT
// Performance instrumentation (E6.12): QPC timing around frameTick().
// Logs min/max/avg frame cost every ~600 frames (~10s at 60Hz).
// No per-frame I/O — only OutputDebugStringW on the reporting interval.
static int64_t s_perfFrameCount = 0;
static int64_t s_perfTotalTicks = 0;
static int64_t s_perfMinTicks = INT64_MAX;
static int64_t s_perfMaxTicks = 0;
static constexpr int64_t kPerfReportInterval = 600;
#endif

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
    s_firstTick = true;

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
#ifdef SMOOTHZOOM_PERF_AUDIT
    LARGE_INTEGER perfStart;
    QueryPerformanceCounter(&perfStart);
#endif

    // 0. Check for settings changes (Phase 5B: AC-2.9.04–AC-2.9.09)
    //    One atomic uint64 load per frame (common case). shared_ptr load only on change.
    uint64_t ver = s_state->settingsVersion.load(std::memory_order_acquire);
    if (ver != s_cachedSettingsVersion || s_firstTick)
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
            s_reverseScrollDirection = snap->reverseScrollDirection;

            // Phase 6: Sync color inversion from settings (AC-2.10.03, AC-2.10.04)
            // BF-1: On first tick, force-apply regardless of state match to ensure
            // persisted colorInversionEnabled=true is applied on startup.
            if (snap->colorInversionEnabled != s_colorInversionActive || s_firstTick)
            {
                s_colorInversionActive = snap->colorInversionEnabled;
                s_magBridge.setColorInversion(s_colorInversionActive);
            }
        }
        s_cachedSettingsVersion = ver;
        s_firstTick = false;
        SZ_LOG_DEBUG("RenderLoop", L"Settings applied (version %llu)", static_cast<unsigned long long>(ver));
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
        if (s_reverseScrollDirection)
            scrollDelta = -scrollDelta;
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

    // Per-frame active monitor detection (AC-MM.04, E6.4–E6.7)
    // MonitorFromPoint and GetMonitorInfo are lightweight shared-memory reads (~1µs),
    // no heap allocation, no mutex — safe for the hot path.
    static HMONITOR s_activeMonitor = nullptr;
    POINT cursorPt = {rawPtrX, rawPtrY};
    HMONITOR hMon = MonitorFromPoint(cursorPt, MONITOR_DEFAULTTONEAREST);
    MONITORINFO monInfo = {};
    monInfo.cbSize = sizeof(monInfo);
    GetMonitorInfo(hMon, &monInfo);

    int32_t monLeft   = monInfo.rcMonitor.left;
    int32_t monTop    = monInfo.rcMonitor.top;
    int32_t monWidth  = monInfo.rcMonitor.right - monInfo.rcMonitor.left;
    int32_t monHeight = monInfo.rcMonitor.bottom - monInfo.rcMonitor.top;

    // Log on monitor transition (state transition only, not per-frame)
    if (hMon != s_activeMonitor) {
        s_activeMonitor = hMon;
        SZ_LOG_INFO("RenderLoop", L"Monitor transition: rect=(%d,%d %dx%d)",
                    monLeft, monTop, monWidth, monHeight);
    }

    // Per-monitor deadzone scaling (AC-MM.04)
    int32_t deadzoneThreshold = monHeight > 0 ? (3 * monHeight / 1080) : 3;
    if (deadzoneThreshold < 1) deadzoneThreshold = 1;

    if (!s_deadzoneInitialized)
    {
        s_committedPtrX = rawPtrX;
        s_committedPtrY = rawPtrY;
        s_lastRawPtrX = rawPtrX;
        s_lastRawPtrY = rawPtrY;
        s_deadzoneInitialized = true;
    }

    // WS2A: Update timestamp on ANY raw pointer movement (even sub-deadzone).
    // This ensures determineActiveSource() correctly favors Pointer when the
    // user is moving the mouse, even if the movement is within the deadzone.
    bool rawMoved = (rawPtrX != s_lastRawPtrX || rawPtrY != s_lastRawPtrY);
    if (rawMoved)
    {
        s_lastPointerMoveTimeMs = currentTimeMs();
        s_lastRawPtrX = rawPtrX;
        s_lastRawPtrY = rawPtrY;
    }

    // Deadzone gates the committed position used for viewport offset calculation.
    int32_t dx = rawPtrX - s_committedPtrX;
    int32_t dy = rawPtrY - s_committedPtrY;
    bool pointerMoved = (dx * dx + dy * dy > deadzoneThreshold * deadzoneThreshold);
    if (pointerMoved)
    {
        s_committedPtrX = rawPtrX;
        s_committedPtrY = rawPtrY;
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
        // Use caret's monitor for centering (AC-MM.04)
        {
            ScreenPoint caretCenter = caretRect.center();
            POINT cp = {caretCenter.x, caretCenter.y};
            HMONITOR hElemMon = MonitorFromPoint(cp, MONITOR_DEFAULTTONEAREST);
            MONITORINFO emi = {}; emi.cbSize = sizeof(emi);
            GetMonitorInfo(hElemMon, &emi);
            int32_t eMonL = emi.rcMonitor.left, eMonT = emi.rcMonitor.top;
            int32_t eMonW = emi.rcMonitor.right - eMonL;
            int32_t eMonH = emi.rcMonitor.bottom - eMonT;
            targetOffset = ViewportTracker::computeCaretOffset(
                caretRect, zoom, eMonW, eMonH, eMonL, eMonT);
        }
        break;
    case TrackingSource::Focus:
        // Use focus element's monitor for centering (AC-MM.04)
        {
            ScreenPoint focusCenter = focusRect.center();
            POINT fp = {focusCenter.x, focusCenter.y};
            HMONITOR hElemMon = MonitorFromPoint(fp, MONITOR_DEFAULTTONEAREST);
            MONITORINFO emi = {}; emi.cbSize = sizeof(emi);
            GetMonitorInfo(hElemMon, &emi);
            int32_t eMonL = emi.rcMonitor.left, eMonT = emi.rcMonitor.top;
            int32_t eMonW = emi.rcMonitor.right - eMonL;
            int32_t eMonH = emi.rcMonitor.bottom - eMonT;
            targetOffset = ViewportTracker::computeElementOffset(
                focusRect, zoom, eMonW, eMonH, eMonL, eMonT);
        }
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
        SZ_LOG_DEBUG("RenderLoop", L"Source transition: -> %d", static_cast<int>(newSource));
    }

    // WS2B: Cancel active transition if pointer moves beyond deadzone.
    // Prevents viewport drifting toward stale focus/caret target when user moves mouse.
    if (s_sourceTransitionActive && s_activeSource != TrackingSource::Pointer && pointerMoved)
    {
        s_sourceTransitionActive = false;
        s_activeSource = TrackingSource::Pointer;
        targetOffset = ViewportTracker::computePointerOffset(
            s_committedPtrX, s_committedPtrY, zoom, s_screenW, s_screenH,
            s_screenOriginX, s_screenOriginY);
        SZ_LOG_DEBUG("RenderLoop", L"Source transition cancelled by pointer movement");
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
            SZ_LOG_ERROR("RenderLoop", L"MagBridge setTransform failed (zoom=%.2f, off=%.0f,%.0f)", zoom, offset.x, offset.y);
        else if (ok && !s_magBridgeLastOk)
            SZ_LOG_INFO("RenderLoop", L"MagBridge setTransform recovered");
        s_magBridgeLastOk = ok;

        s_lastZoom = zoom;
        s_lastOffX = offset.x;
        s_lastOffY = offset.y;

        // Phase 5C: publish for main thread (graceful exit, tray tooltip)
        s_state->currentZoomLevel.store(zoom, std::memory_order_relaxed);
    }

#ifdef SMOOTHZOOM_PERF_AUDIT
    LARGE_INTEGER perfEnd;
    QueryPerformanceCounter(&perfEnd);
    int64_t elapsed = perfEnd.QuadPart - perfStart.QuadPart;
    s_perfTotalTicks += elapsed;
    if (elapsed < s_perfMinTicks) s_perfMinTicks = elapsed;
    if (elapsed > s_perfMaxTicks) s_perfMaxTicks = elapsed;
    ++s_perfFrameCount;

    if (s_perfFrameCount >= kPerfReportInterval && s_qpcFrequency > 0)
    {
        double toUs = 1000000.0 / static_cast<double>(s_qpcFrequency);
        double avgUs = (static_cast<double>(s_perfTotalTicks) / s_perfFrameCount) * toUs;
        double minUs = static_cast<double>(s_perfMinTicks) * toUs;
        double maxUs = static_cast<double>(s_perfMaxTicks) * toUs;

        wchar_t buf[256];
        _snwprintf_s(buf, _countof(buf), _TRUNCATE,
            L"SmoothZoom PERF: %lld frames, avg=%.1fus, min=%.1fus, max=%.1fus\n",
            s_perfFrameCount, avgUs, minUs, maxUs);
        OutputDebugStringW(buf);

        s_perfFrameCount = 0;
        s_perfTotalTicks = 0;
        s_perfMinTicks = INT64_MAX;
        s_perfMaxTicks = 0;
    }
#endif
}

} // namespace SmoothZoom

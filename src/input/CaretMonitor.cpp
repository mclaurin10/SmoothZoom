// =============================================================================
// SmoothZoom — CaretMonitor
// UIA TextPattern + GTTI polling fallback. Doc 3 §3.6
//
// Polls GetGUIThreadInfo at 30Hz for caret position. This is the primary
// technique because it works across the widest range of applications (Notepad,
// Terminal, WordPad, etc.) without requiring UIA TextPattern support.
//
// Phase 3 also adds UIA TextPattern subscription when available, but the
// GTTI fallback runs continuously and provides the baseline.
//
// Writes to SharedState::caretRect via SeqLock.
// Silent degradation when caret unavailable (AC-2.6.11).
// =============================================================================

#include "smoothzoom/input/CaretMonitor.h"
#include "smoothzoom/common/SharedState.h"
#include "smoothzoom/common/RectValidation.h"

#ifndef SMOOTHZOOM_TESTING

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <windows.h>
#include <chrono>
#include <thread>

namespace SmoothZoom
{

// Steady-clock ms — same time base as RenderLoop's arbitration timestamps
// (currentTimeMs in RenderLoop.cpp and FocusMonitor's lastFocusChangeTime).
static int64_t steadyNowMs()
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

// Validate a caret rectangle.
// checkBounds: only on SCREEN-coordinate rects. The client-coordinate rect (pre
// ClientToScreen) must NOT be bounds-checked against virtual-desktop screen
// space — non-DPI-aware source windows report virtualized client coords (R-10),
// so a screen-space bounds test there is a category error.
static bool isValidCaretRect(const RECT& r, const SharedState* st, bool checkBounds)
{
    // Caret rects can be very thin (1px or 0px wide for a text cursor) —
    // allow zero width, but require non-negative dimensions and some height.
    int32_t w = r.right - r.left;
    int32_t h = r.bottom - r.top;
    if (w < 0 || h <= 0) return false;
    if (!checkBounds) return true;
    // Reject rects entirely off the virtual desktop, using the live bounds
    // (R-09; handles negative-origin / large multi-monitor layouts).
    const int32_t vx = st->screenOriginX.load(std::memory_order_relaxed);
    const int32_t vy = st->screenOriginY.load(std::memory_order_relaxed);
    const int32_t vw = st->screenWidth.load(std::memory_order_relaxed);
    const int32_t vh = st->screenHeight.load(std::memory_order_relaxed);
    return rectIntersectsVirtualDesktop(r.left, r.top, r.right, r.bottom, vx, vy, vw, vh);
}

// ─── CaretMonitor::Impl ──────────────────────────────────────────────────

struct CaretMonitor::Impl
{
    SharedState* state = nullptr;
    std::thread pollThread;
    std::atomic<bool> stopRequested{false};

    void pollLoop()
    {
        // Poll GTTI at ~30Hz (33ms interval) — Doc 3 §3.6
        // Sufficient for human typing speed (5–15 chars/sec)
        while (!stopRequested.load(std::memory_order_acquire))
        {
            pollGTTI();
            Sleep(33); // ~30Hz
        }
    }

    void pollGTTI()
    {
        GUITHREADINFO gti{};
        gti.cbSize = sizeof(gti);

        if (!GetGUIThreadInfo(0, &gti)) return; // Silent failure (AC-2.6.11)

        // Gate on "is there a caret," not "is it currently blinking-visible."
        // GUI_CARETBLINKING is set only during the caret's visible blink phase, so
        // gating on it stops stamping freshness during the dark half of every
        // blink. RenderLoop's freshness window (kCaretFreshnessMs = 150) is shorter
        // than a default ~530ms blink half-period, so a typing pause could let the
        // caret go stale and oscillate the viewport source. hwndCaret + a valid
        // rcCaret means a caret exists; a caret-less app reports hwndCaret == NULL
        // (and isValidCaretRect rejects an empty rect), so we still degrade silently
        // where no caret can be determined (AC-2.6.07, AC-2.6.11; R-09).
        if (!gti.hwndCaret) return;

        RECT caretClient = gti.rcCaret;

        // Validate in client coords first (degeneracy only — see isValidCaretRect)
        if (!isValidCaretRect(caretClient, state, /*checkBounds=*/false)) return;

        // Convert from client coordinates to screen coordinates
        POINT topLeft = {caretClient.left, caretClient.top};
        POINT bottomRight = {caretClient.right, caretClient.bottom};

        if (!ClientToScreen(gti.hwndCaret, &topLeft)) return;
        if (!ClientToScreen(gti.hwndCaret, &bottomRight)) return;

        RECT screenRect;
        screenRect.left   = topLeft.x;
        screenRect.top    = topLeft.y;
        screenRect.right  = bottomRight.x;
        screenRect.bottom = bottomRight.y;

        // Final validation in screen coords (full virtual-desktop bounds check)
        if (!isValidCaretRect(screenRect, state, /*checkBounds=*/true)) return;

        // Write to shared state via SeqLock
        ScreenRect rect;
        rect.left   = screenRect.left;
        rect.top    = screenRect.top;
        rect.right  = screenRect.right;
        rect.bottom = screenRect.bottom;

        state->caretRect.write(rect);

        // Stamp freshness — RenderLoop only treats the caret as a valid
        // tracking source when this is recent (a few poll periods). Failed
        // polls above return without stamping, so a rect from a closed or
        // caret-less window goes stale instead of hijacking arbitration on
        // every keystroke (AC-2.6.11: degrade silently).
        state->lastCaretUpdateTime.store(steadyNowMs(), std::memory_order_release);
    }
};

// ─── CaretMonitor public interface ───────────────────────────────────────

CaretMonitor::~CaretMonitor()
{
    stop();
}

bool CaretMonitor::start(SharedState& state)
{
    if (running_.load(std::memory_order_relaxed))
        return true;

    impl_ = new Impl();
    impl_->state = &state;
    impl_->stopRequested.store(false);

    // Start GTTI polling thread.
    // Spec deviation: Doc 3 §2.1 specifies three threads (Main, Render, UIA).
    // CaretMonitor adds a fourth thread for GTTI polling because UIA caret
    // events are unreliable across apps, and polling must not block the UIA
    // event-driven FocusMonitor thread. This is an intentional deviation.
    impl_->pollThread = std::thread([this]() { impl_->pollLoop(); });

    running_.store(true, std::memory_order_release);
    return true;
}

void CaretMonitor::stop()
{
    if (!running_.load(std::memory_order_acquire))
        return;

    if (impl_)
    {
        impl_->stopRequested.store(true, std::memory_order_release);
        if (impl_->pollThread.joinable())
            impl_->pollThread.join();
        delete impl_;
        impl_ = nullptr;
    }

    running_.store(false, std::memory_order_release);
}

} // namespace SmoothZoom

#else // SMOOTHZOOM_TESTING — stub for non-Win32 test builds

namespace SmoothZoom
{

CaretMonitor::~CaretMonitor() { stop(); }

bool CaretMonitor::start(SharedState& /*state*/)
{
    return true; // No-op in test builds
}

void CaretMonitor::stop()
{
    // No-op in test builds
}

} // namespace SmoothZoom

#endif // SMOOTHZOOM_TESTING

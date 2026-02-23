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

#ifndef SMOOTHZOOM_TESTING

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <windows.h>
#include <thread>

namespace SmoothZoom
{

// Validate a caret rectangle
static bool isValidCaretRect(const RECT& r)
{
    // Caret rects can be very thin (1px wide for text cursor) — allow small width
    int32_t w = r.right - r.left;
    int32_t h = r.bottom - r.top;
    // Must have non-negative dimensions and at least some height
    if (w < 0 || h <= 0) return false;
    // Reject absurdly large
    if (w > 5000 || h > 5000) return false;
    return true;
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

        // Check if there's a blinking caret (GUI_CARETBLINKING flag)
        if (!(gti.flags & GUI_CARETBLINKING)) return;
        if (!gti.hwndCaret) return;

        RECT caretClient = gti.rcCaret;

        // Validate in client coords first
        if (!isValidCaretRect(caretClient)) return;

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

        // Final validation in screen coords
        if (!isValidCaretRect(screenRect)) return;

        // Write to shared state via SeqLock
        ScreenRect rect;
        rect.left   = screenRect.left;
        rect.top    = screenRect.top;
        rect.right  = screenRect.right;
        rect.bottom = screenRect.bottom;

        state->caretRect.write(rect);
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

    // Start GTTI polling thread
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

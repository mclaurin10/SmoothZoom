// =============================================================================
// SmoothZoom — FocusMonitor
// UIA focus-changed event subscription. Doc 3 §3.5
//
// Subscribes to IUIAutomation focus-changed events on a dedicated UIA thread.
// Extracts bounding rectangles, validates them, writes to shared state.
// Debounce logic is in ViewportTracker (AC-2.5.07), not here.
// Graceful degradation: if UIA fails, pointer tracking continues (AC-2.5.14).
// =============================================================================

#include "smoothzoom/input/FocusMonitor.h"
#include "smoothzoom/common/SharedState.h"
#include "smoothzoom/common/RectValidation.h"
#include "smoothzoom/support/Logger.h"

#ifndef SMOOTHZOOM_TESTING

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <windows.h>
#include <uiautomation.h>
#include <chrono>
#include <thread>

#pragma comment(lib, "Ole32.lib")
#pragma comment(lib, "OleAut32.lib")

namespace SmoothZoom
{

// Get current time in milliseconds (monotonic)
static int64_t currentTimeMs()
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

// Validate a bounding rectangle from UIA (R-09: UIA Inconsistency)
static bool isValidRect(const RECT& r, const SharedState* st)
{
    int32_t w = r.right - r.left;
    int32_t h = r.bottom - r.top;
    // Reject zero-area or negative rectangles.
    if (w <= 0 || h <= 0) return false;
    // Reject rects entirely off the virtual desktop, using the live bounds
    // (handles negative-origin / large multi-monitor layouts that fixed
    // magic-number limits wrongly rejected).
    const int32_t vx = st->screenOriginX.load(std::memory_order_relaxed);
    const int32_t vy = st->screenOriginY.load(std::memory_order_relaxed);
    const int32_t vw = st->screenWidth.load(std::memory_order_relaxed);
    const int32_t vh = st->screenHeight.load(std::memory_order_relaxed);
    return rectIntersectsVirtualDesktop(r.left, r.top, r.right, r.bottom, vx, vy, vw, vh);
}

// ─── Focus Changed Event Handler (COM class) ──────────────────────────────

class FocusChangedHandler : public IUIAutomationFocusChangedEventHandler
{
public:
    explicit FocusChangedHandler(SharedState* state) : state_(state) {}

    // IUnknown
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refCount_; }
    ULONG STDMETHODCALLTYPE Release() override
    {
        ULONG count = --refCount_;
        if (count == 0) delete this;
        return count;
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override
    {
        if (riid == __uuidof(IUnknown) ||
            riid == __uuidof(IUIAutomationFocusChangedEventHandler))
        {
            *ppv = static_cast<IUIAutomationFocusChangedEventHandler*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }

    // IUIAutomationFocusChangedEventHandler
    HRESULT STDMETHODCALLTYPE HandleFocusChangedEvent(
        IUIAutomationElement* sender) override
    {
        if (!sender || !state_) return S_OK;

        // Direct call — bounded by IUIAutomation6::ConnectionTimeout (R-11)
        auto start = std::chrono::steady_clock::now();
        RECT boundingRect{};
        HRESULT hr = sender->get_CurrentBoundingRectangle(&boundingRect);
        auto elapsed = std::chrono::steady_clock::now() - start;
        auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();

        // Log slow callbacks (R-11 mitigation item 3)
        if (elapsedMs > 50) {
            SZ_LOG_WARN("FocusMonitor", L"Slow UIA bounding rect query: %lldms", elapsedMs);
        }

        if (FAILED(hr)) return S_OK; // Silent degradation (AC-2.5.14)
        if (!isValidRect(boundingRect, state_)) return S_OK; // Reject bad rects (R-09)

        // Write validated rectangle to shared state via SeqLock
        ScreenRect rect;
        rect.left   = boundingRect.left;
        rect.top    = boundingRect.top;
        rect.right  = boundingRect.right;
        rect.bottom = boundingRect.bottom;

        state_->focusRect.write(rect);
        state_->lastFocusChangeTime.store(currentTimeMs(), std::memory_order_release);

        return S_OK;
    }

private:
    SharedState* state_ = nullptr;
    std::atomic<ULONG> refCount_{1};
};

// ─── FocusMonitor::Impl ──────────────────────────────────────────────────

struct FocusMonitor::Impl
{
    IUIAutomation* automation = nullptr;
    FocusChangedHandler* handler = nullptr;
    std::thread uiaThread;
    std::atomic<bool> stopRequested{false};
    SharedState* state = nullptr;

    void threadMain()
    {
        // Initialize COM on UIA thread (Doc 3 §2.3)
        HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (FAILED(hr)) return;

        // Create the UIA root via the CUIAutomation8 coclass, not CUIAutomation.
        // Only CUIAutomation8 vends the newer IUIAutomation2..7 interfaces; the
        // legacy CUIAutomation coclass returns an object whose QueryInterface for
        // IUIAutomation6 fails with E_NOINTERFACE even on OSes that fully support
        // it (observed on Win11 26200), which silently disabled the R-11
        // ConnectionTimeout mitigation. CUIAutomation8 has shipped since Win8.
        hr = CoCreateInstance(__uuidof(CUIAutomation8), nullptr,
                              CLSCTX_INPROC_SERVER, __uuidof(IUIAutomation),
                              reinterpret_cast<void**>(&automation));
        if (FAILED(hr))
        {
            // Fallback for genuinely older OSes that lack the CUIAutomation8
            // coclass (pre-Win8). IUIAutomation6 won't be available there, so the
            // QI below skips the timeout and tracking degrades gracefully (R-09).
            SZ_LOG_WARN("FocusMonitor",
                        L"CUIAutomation8 unavailable (hr=0x%08lX) — falling back to CUIAutomation",
                        static_cast<unsigned long>(hr));
            hr = CoCreateInstance(__uuidof(CUIAutomation), nullptr,
                                  CLSCTX_INPROC_SERVER, __uuidof(IUIAutomation),
                                  reinterpret_cast<void**>(&automation));
        }
        if (FAILED(hr))
        {
            CoUninitialize();
            return;
        }

        // Set 100ms timeout for UIA property queries (R-11 mitigation). IUIAutomation6
        // ships in Win10 1809+ and is reachable from the CUIAutomation8 coclass above.
        IUIAutomation6* automation6 = nullptr;
        HRESULT hrTimeout = automation->QueryInterface(
            __uuidof(IUIAutomation6),
            reinterpret_cast<void**>(&automation6));
        if (SUCCEEDED(hrTimeout) && automation6) {
            automation6->put_ConnectionTimeout(100);  // 100ms
            automation6->Release();
            SZ_LOG_INFO("FocusMonitor", L"UIA connection timeout set to 100ms");
        } else {
            SZ_LOG_WARN("FocusMonitor",
                        L"IUIAutomation6 unavailable (hr=0x%08lX) — no query timeout",
                        static_cast<unsigned long>(hrTimeout));
        }

        handler = new FocusChangedHandler(state);
        hr = automation->AddFocusChangedEventHandler(nullptr, handler);
        if (FAILED(hr))
        {
            handler->Release();
            handler = nullptr;
            automation->Release();
            automation = nullptr;
            CoUninitialize();
            return;
        }

        // Message pump for UIA event delivery
        MSG msg;
        while (!stopRequested.load(std::memory_order_acquire))
        {
            // Process UIA messages with timeout to check stop flag
            BOOL gotMsg = PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE);
            if (gotMsg)
            {
                if (msg.message == WM_QUIT) break;
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
            else
            {
                // No message — sleep briefly to avoid busy-wait
                Sleep(10);
            }
        }

        // Cleanup
        if (automation && handler)
        {
            automation->RemoveFocusChangedEventHandler(handler);
            handler->Release();
            handler = nullptr;
        }
        if (automation)
        {
            automation->Release();
            automation = nullptr;
        }
        CoUninitialize();
    }
};

// ─── FocusMonitor public interface ───────────────────────────────────────

FocusMonitor::~FocusMonitor()
{
    stop();
}

bool FocusMonitor::start(SharedState& state)
{
    if (running_.load(std::memory_order_relaxed))
        return true;

    impl_ = new Impl();
    impl_->state = &state;
    impl_->stopRequested.store(false);

    impl_->uiaThread = std::thread([this]() { impl_->threadMain(); });

    running_.store(true, std::memory_order_release);
    return true;
}

void FocusMonitor::stop()
{
    if (!running_.load(std::memory_order_acquire))
        return;

    if (impl_)
    {
        impl_->stopRequested.store(true, std::memory_order_release);
        if (impl_->uiaThread.joinable())
            impl_->uiaThread.join();
        delete impl_;
        impl_ = nullptr;
    }

    running_.store(false, std::memory_order_release);
}

} // namespace SmoothZoom

#else // SMOOTHZOOM_TESTING — stub for non-Win32 test builds

namespace SmoothZoom
{

FocusMonitor::~FocusMonitor() { stop(); }

bool FocusMonitor::start(SharedState& /*state*/)
{
    return true; // No-op in test builds
}

void FocusMonitor::stop()
{
    // No-op in test builds
}

} // namespace SmoothZoom

#endif // SMOOTHZOOM_TESTING

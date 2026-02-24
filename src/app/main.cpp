// =============================================================================
// SmoothZoom — Application Entry Point
// WinMain, message pump, component wiring, lifecycle. Doc 3 §2.1
//
// Threading model (3 threads):
//   Main Thread: Message pump, low-level hooks, TrayUI, app lifecycle.
//   Render Thread: VSync-locked frame ticks via DwmFlush(), calls MagBridge.
//   UIA Thread: (Phase 3) UI Automation subscriptions.
//
// Phase 1: Crash handler (R-14), hook watchdog (R-05), WM_ENDSESSION.
// =============================================================================

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <windows.h>

#include "smoothzoom/common/SharedState.h"
#include "smoothzoom/input/InputInterceptor.h"
#include "smoothzoom/input/FocusMonitor.h"
#include "smoothzoom/input/CaretMonitor.h"
#include "smoothzoom/logic/RenderLoop.h"
#include "smoothzoom/output/MagBridge.h"

// Global shared state — single instance, lifetime = application
static SmoothZoom::SharedState g_sharedState;

// Components
static SmoothZoom::InputInterceptor g_inputInterceptor;
static SmoothZoom::RenderLoop g_renderLoop;
static SmoothZoom::FocusMonitor g_focusMonitor;   // Phase 3: UIA focus tracking
static SmoothZoom::CaretMonitor g_caretMonitor;    // Phase 3: text caret tracking

// Hook watchdog timer ID and interval (R-05, AC-ERR.03)
static constexpr UINT_PTR kWatchdogTimerId = 1;
static constexpr UINT kWatchdogIntervalMs = 5000;

// Crash handler (R-14): reset magnification on unhandled exception
static LONG WINAPI crashHandler(EXCEPTION_POINTERS* /*exInfo*/)
{
    // Best-effort zoom reset — MagBridge::shutdown() resets to 1.0× and uninitializes.
    // We create a temporary MagBridge here because the crash may have corrupted
    // our main MagBridge state. The Magnification API is global per-process,
    // so resetting from any valid instance works.
    SmoothZoom::MagBridge emergencyBridge;
    if (emergencyBridge.initialize())
    {
        emergencyBridge.shutdown();
    }

    return EXCEPTION_CONTINUE_SEARCH;
}

// Hidden message-only window for receiving WM_TIMER and WM_ENDSESSION
static HWND g_msgWindow = nullptr;

static LRESULT CALLBACK msgWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_TIMER:
        if (wParam == kWatchdogTimerId)
        {
            // Hook health watchdog (R-05, AC-ERR.03):
            // Check if hooks were silently deregistered and reinstall if needed.
            if (!g_inputInterceptor.isHealthy())
            {
                OutputDebugStringW(L"SmoothZoom: Hook deregistration detected, reinstalling...\n");
                g_inputInterceptor.reinstall();
            }
        }
        return 0;

    case WM_ENDSESSION:
        if (wParam)
        {
            // System is shutting down or user is logging off.
            // Reset zoom to prevent stuck magnification (R-14).
            g_caretMonitor.stop();
            g_focusMonitor.stop();
            g_renderLoop.requestShutdown();

            // Brief wait for render thread to reset zoom
            for (int i = 0; i < 50 && g_renderLoop.isRunning(); ++i)
                Sleep(10);

            if (!g_renderLoop.isRunning())
            {
                g_renderLoop.finalizeShutdown();
                g_inputInterceptor.uninstall();
            }
            // else: render thread still alive — process exit will clean up
        }
        return 0;

    default:
        return DefWindowProcW(hWnd, msg, wParam, lParam);
    }
}

static HWND createMessageWindow(HINSTANCE hInstance)
{
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.lpfnWndProc = msgWndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"SmoothZoomMsgWindow";

    RegisterClassExW(&wc);

    return CreateWindowExW(
        0, L"SmoothZoomMsgWindow", nullptr,
        0, 0, 0, 0, 0,
        HWND_MESSAGE, // Message-only window
        nullptr, hInstance, nullptr);
}

int WINAPI wWinMain(
    _In_ HINSTANCE hInstance,
    _In_opt_ HINSTANCE /*hPrevInstance*/,
    _In_ LPWSTR /*lpCmdLine*/,
    _In_ int /*nCmdShow*/)
{
    // ── 0. Install crash handler (R-14) ──────────────────────────────────────
    SetUnhandledExceptionFilter(crashHandler);

    // ── 1. Install input hooks (must be on a thread with a message pump) ────
    if (!g_inputInterceptor.install(g_sharedState))
    {
        MessageBoxW(nullptr,
                    L"Failed to install input hooks.\n\n"
                    L"This may be caused by:\n"
                    L"  - Security software blocking hook installation\n"
                    L"  - Another application holding exclusive hook access\n\n"
                    L"SmoothZoom cannot function without input hooks.",
                    L"SmoothZoom \u2014 Startup Error",
                    MB_OK | MB_ICONERROR);
        return 1;
    }

    // ── 2. Start render loop (launches render thread, initializes MagBridge) ─
    g_renderLoop.start(g_sharedState);

    if (!g_renderLoop.isRunning())
    {
        g_inputInterceptor.uninstall();
        MessageBoxW(nullptr,
                    L"Failed to initialize the Magnification API.\n\n"
                    L"This may be caused by:\n"
                    L"  - Binary is not code-signed (R-12)\n"
                    L"  - Binary is not running from a secure folder\n"
                    L"    (e.g., C:\\Program Files\\SmoothZoom\\)\n"
                    L"  - uiAccess=\"true\" manifest not embedded\n"
                    L"  - Another full-screen magnifier is active\n\n"
                    L"See README.md for signing and deployment instructions.",
                    L"SmoothZoom \u2014 Magnification API Error",
                    MB_OK | MB_ICONERROR);
        return 1;
    }

    // ── 2b. Start UIA monitoring (Phase 3: focus + caret tracking) ──────────
    // These run on a dedicated UIA thread. Failure is non-fatal (AC-2.5.14, AC-2.6.11).
    g_focusMonitor.start(g_sharedState);
    g_caretMonitor.start(g_sharedState);

    // ── 2c. Create message window for watchdog timer + WM_ENDSESSION ────────
    g_msgWindow = createMessageWindow(hInstance);
    if (g_msgWindow)
    {
        SetTimer(g_msgWindow, kWatchdogTimerId, kWatchdogIntervalMs, nullptr);
    }

    // ── 3. Run Win32 message pump ───────────────────────────────────────────
    // Low-level hooks require a message pump on the installing thread.
    // The pump runs until WM_QUIT is posted (by Ctrl+Q via InputInterceptor).
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0)
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    // ── 4. Shutdown sequence ────────────────────────────────────────────────
    if (g_msgWindow)
    {
        KillTimer(g_msgWindow, kWatchdogTimerId);
        DestroyWindow(g_msgWindow);
        g_msgWindow = nullptr;
    }

    // Stop UIA monitors first (they write to shared state read by render thread)
    g_caretMonitor.stop();
    g_focusMonitor.stop();

    g_renderLoop.requestShutdown();

    // Wait for render thread to finish (it resets zoom to 1.0× on its thread)
    while (g_renderLoop.isRunning())
    {
        Sleep(10);
    }

    // Finalize shutdown on main thread (MagUninitialize must be main thread)
    g_renderLoop.finalizeShutdown();

    g_inputInterceptor.uninstall();

    return 0;
}

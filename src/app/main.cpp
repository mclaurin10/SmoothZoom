// =============================================================================
// SmoothZoom — Application Entry Point
// WinMain, message pump, component wiring, lifecycle. Doc 3 §2.1
//
// Threading model (3 threads):
//   Main Thread: Message pump, low-level hooks, TrayUI, app lifecycle.
//   Render Thread: VSync-locked frame ticks via DwmFlush(), calls MagBridge.
//   UIA Thread: (Phase 3) UI Automation subscriptions.
//
// Phase 0: Main thread + Render thread. Ctrl+Q exits.
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
#include "smoothzoom/logic/RenderLoop.h"

// Global shared state — single instance, lifetime = application
static SmoothZoom::SharedState g_sharedState;

// Components
static SmoothZoom::InputInterceptor g_inputInterceptor;
static SmoothZoom::RenderLoop g_renderLoop;

int WINAPI wWinMain(
    _In_ HINSTANCE /*hInstance*/,
    _In_opt_ HINSTANCE /*hPrevInstance*/,
    _In_ LPWSTR /*lpCmdLine*/,
    _In_ int /*nCmdShow*/)
{
    // ── 1. Install input hooks (must be on a thread with a message pump) ────
    if (!g_inputInterceptor.install(g_sharedState))
    {
        MessageBoxW(nullptr,
                    L"Failed to install input hooks.\n\n"
                    L"This may be caused by:\n"
                    L"  - Security software blocking hook installation\n"
                    L"  - Another application holding exclusive hook access\n\n"
                    L"SmoothZoom cannot function without input hooks.",
                    L"SmoothZoom — Startup Error",
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
                    L"SmoothZoom — Magnification API Error",
                    MB_OK | MB_ICONERROR);
        return 1;
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

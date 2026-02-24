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
#include "smoothzoom/support/SettingsManager.h"
#include "smoothzoom/support/TrayUI.h"

#include <memory>
#include <string>

// Global shared state — single instance, lifetime = application
static SmoothZoom::SharedState g_sharedState;

// Components
static SmoothZoom::InputInterceptor g_inputInterceptor;
static SmoothZoom::RenderLoop g_renderLoop;
static SmoothZoom::FocusMonitor g_focusMonitor;   // Phase 3: UIA focus tracking
static SmoothZoom::CaretMonitor g_caretMonitor;    // Phase 3: text caret tracking
static SmoothZoom::SettingsManager g_settingsManager; // Phase 5: config persistence
static SmoothZoom::TrayUI g_trayUI;                    // Phase 5C: tray icon + settings
static std::string g_configPath;                      // Resolved at startup

// Hook watchdog timer ID and interval (R-05, AC-ERR.03)
static constexpr UINT_PTR kWatchdogTimerId = 1;
static constexpr UINT kWatchdogIntervalMs = 5000;

// Phase 5B: Application-defined message for settings window (AC-2.8.11)
static constexpr UINT WM_OPEN_SETTINGS = WM_APP + 1;
// Phase 5C: Tray icon callback and graceful exit messages
static constexpr UINT WM_TRAYICON = WM_APP + 2;
static constexpr UINT WM_GRACEFUL_EXIT = WM_APP + 3;
// Phase 5C: Context menu command IDs (must match TrayUI.cpp)
static constexpr UINT IDM_SETTINGS    = 40001;
static constexpr UINT IDM_TOGGLE_ZOOM = 40002;
static constexpr UINT IDM_EXIT        = 40003;
// Explorer restart detection
static UINT WM_TASKBAR_CREATED = 0;

// Phase 5B: Observer callback — publishes settings changes to SharedState.
// Runs on main thread. Render thread detects via settingsVersion atomic.
static void publishToSharedState(const SmoothZoom::SettingsSnapshot& s, void* ud)
{
    auto* state = static_cast<SmoothZoom::SharedState*>(ud);
    auto snap = std::make_shared<const SmoothZoom::SettingsSnapshot>(s);
    std::atomic_store(&state->settingsSnapshot, snap);
    state->settingsVersion.fetch_add(1, std::memory_order_release);
}

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
        // Phase 5C: Graceful exit polling
        else if (g_trayUI.isExitPending() && g_trayUI.checkExitPoll())
        {
            PostQuitMessage(0);
        }
        return 0;

    case WM_OPEN_SETTINGS:
        g_trayUI.showSettingsWindow();
        return 0;

    case WM_TRAYICON:
        g_trayUI.onTrayMessage(lParam);
        return 0;

    case WM_GRACEFUL_EXIT:
        g_trayUI.requestGracefulExit();
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case IDM_SETTINGS:    g_trayUI.showSettingsWindow(); return 0;
        case IDM_TOGGLE_ZOOM: g_sharedState.commandQueue.push(SmoothZoom::ZoomCommand::TrayToggle); return 0;
        case IDM_EXIT:        g_trayUI.requestGracefulExit(); return 0;
        }
        break;

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
        // Explorer restart: re-add tray icon
        if (WM_TASKBAR_CREATED && msg == WM_TASKBAR_CREATED)
        {
            g_trayUI.recreateTrayIcon();
            return 0;
        }
        return DefWindowProcW(hWnd, msg, wParam, lParam);
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
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

    // ── 0b. Load settings (Phase 5B: AC-2.9.01, AC-2.9.02) ─────────────────
    // Register observers BEFORE loading so the initial load triggers them.
    g_settingsManager.addObserver(publishToSharedState, &g_sharedState);
    g_configPath = SmoothZoom::SettingsManager::getDefaultConfigPath();
    if (!g_configPath.empty())
        g_settingsManager.loadFromFile(g_configPath.c_str());
    // Ensure SharedState has settings even if load failed (defaults apply):
    {
        auto snap = g_settingsManager.snapshot();
        std::atomic_store(&g_sharedState.settingsSnapshot, snap);
        g_sharedState.settingsVersion.store(
            g_settingsManager.version(), std::memory_order_release);
    }

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

    // Phase 5B: Register InputInterceptor for settings changes (AC-2.9.04)
    SmoothZoom::InputInterceptor::registerSettingsObserver(g_settingsManager);

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
        // Phase 5B: Give InputInterceptor the msg window for Win+Ctrl+M (AC-2.8.11)
        SmoothZoom::InputInterceptor::setMessageWindow(g_msgWindow);
    }

    // Phase 5C: Register TaskbarCreated for Explorer restart detection
    WM_TASKBAR_CREATED = RegisterWindowMessageW(L"TaskbarCreated");

    // ── 2d. Create tray icon (Phase 5C: AC-2.9.13–16) ──────────────────────
    g_trayUI.create(hInstance, g_msgWindow, g_sharedState, g_settingsManager,
                    g_configPath.c_str());

    // ── 2e. Start-zoomed (Phase 5C: AC-2.9.18–19) ──────────────────────────
    {
        auto snap = g_settingsManager.snapshot();
        if (snap && snap->startZoomed && snap->defaultZoomLevel > 1.0f)
            g_sharedState.commandQueue.push(SmoothZoom::ZoomCommand::TrayToggle);
    }

    // ── 3. Run Win32 message pump ───────────────────────────────────────────
    // Low-level hooks require a message pump on the installing thread.
    // The pump runs until WM_QUIT is posted (by Ctrl+Q via InputInterceptor).
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0)
    {
        // Phase 5C: Tab navigation in settings window
        HWND hSettings = g_trayUI.settingsHwnd();
        if (hSettings && IsDialogMessage(hSettings, &msg))
            continue;

        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    // ── 4. Shutdown sequence ────────────────────────────────────────────────

    // Phase 5C: Remove tray icon promptly
    g_trayUI.destroy();

    // Phase 5B: Save settings on clean exit (AC-2.9.02)
    if (!g_configPath.empty())
        g_settingsManager.saveToFile(g_configPath.c_str());

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

    // No-op — MagUninitialize now happens on the render thread (thread affinity).
    g_renderLoop.finalizeShutdown();

    g_inputInterceptor.uninstall();

    return 0;
}

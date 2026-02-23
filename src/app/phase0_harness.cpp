// =============================================================================
// SmoothZoom — Phase 0 Foundation & Risk Spike
// Standalone test harness. Doc 4 §3.0.3
//
// This is a MINIMAL entry point that validates three assumptions:
//   1. MagSetFullscreenTransform accepts float zoom (sub-integer precision)
//   2. API latency is sub-frame (≤16ms)
//   3. WH_MOUSE_LL hook can intercept + consume scroll system-wide (UIAccess)
//
// Unlike main.cpp, this harness does NOT use the multi-threaded component
// architecture. It runs everything on one thread for simplicity — the point
// is to validate API behavior, not architecture correctness.
//
// Controls:
//   Hold LWin + scroll wheel → zoom in/out (simple linear model)
//   Release LWin             → retain current zoom level
//   Ctrl+Q                   → reset to 1.0× and exit
// =============================================================================

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <windows.h>
#include <magnification.h>
#include <dwmapi.h>
#include <cstdio>

#pragma comment(lib, "Magnification.lib")
#pragma comment(lib, "Dwmapi.lib")
#pragma comment(lib, "User32.lib")

// ─── Global state ───────────────────────────────────────────────────────────
static float g_currentZoom = 1.0f;
static const float kMinZoom = 1.0f;
static const float kMaxZoom = 10.0f;
static const float kZoomPerNotch = 0.1f; // 10% per scroll notch
static const float kWheelDelta = 120.0f;
static HHOOK g_mouseHook = nullptr;
static bool g_running = true;

// ─── Mouse hook ─────────────────────────────────────────────────────────────
static LRESULT CALLBACK mouseHookProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode >= 0 && wParam == WM_MOUSEWHEEL)
    {
        bool winHeld = (GetAsyncKeyState(VK_LWIN) & 0x8000) != 0 ||
                       (GetAsyncKeyState(VK_RWIN) & 0x8000) != 0;

        if (winHeld)
        {
            auto* info = reinterpret_cast<MSLLHOOKSTRUCT*>(lParam);
            int16_t delta = static_cast<int16_t>(HIWORD(info->mouseData));
            float normalized = static_cast<float>(delta) / kWheelDelta;

            // Simple linear zoom change
            float change = g_currentZoom * kZoomPerNotch * normalized;
            g_currentZoom += change;

            // Clamp
            if (g_currentZoom < kMinZoom)
                g_currentZoom = kMinZoom;
            if (g_currentZoom > kMaxZoom)
                g_currentZoom = kMaxZoom;

            // Apply immediately — hardcoded center offset
            int screenW = GetSystemMetrics(SM_CXSCREEN);
            int screenH = GetSystemMetrics(SM_CYSCREEN);
            int xOff = static_cast<int>((screenW / 2.0f) * (1.0f - 1.0f / g_currentZoom));
            int yOff = static_cast<int>((screenH / 2.0f) * (1.0f - 1.0f / g_currentZoom));

            MagSetFullscreenTransform(g_currentZoom, xOff, yOff);

            // Consume the scroll event
            return 1;
        }
    }

    return CallNextHookEx(g_mouseHook, nCode, wParam, lParam);
}

// ─── Entry point ────────────────────────────────────────────────────────────
int WINAPI wWinMain(
    _In_ HINSTANCE /*hInstance*/,
    _In_opt_ HINSTANCE /*hPrevInstance*/,
    _In_ LPWSTR /*lpCmdLine*/,
    _In_ int /*nCmdShow*/)
{
    // Allocate a console for debug output
    AllocConsole();
    FILE* fp = nullptr;
    freopen_s(&fp, "CONOUT$", "w", stdout);

    printf("=== SmoothZoom Phase 0 Risk Spike ===\n");
    printf("Controls:\n");
    printf("  Hold Win + Scroll  = Zoom in/out\n");
    printf("  Release Win        = Keep current zoom\n");
    printf("  Ctrl+Q             = Reset to 1.0x and exit\n\n");

    // Initialize Magnification API (must succeed or assumptions are invalid)
    if (!MagInitialize())
    {
        DWORD err = GetLastError();
        printf("FATAL: MagInitialize() failed. Error: %lu\n", err);
        printf("Check: signed binary? Secure folder? uiAccess manifest?\n");
        printf("Press any key to exit...\n");
        getchar();
        return 1;
    }
    printf("[OK] MagInitialize() succeeded\n");

    // Show system cursor while magnified
    MagShowSystemCursor(TRUE);

    // E0.1 test: does 1.5× produce visible magnification?
    printf("[TEST E0.1] Setting zoom to 1.5x...\n");
    if (MagSetFullscreenTransform(1.5f, 0, 0))
        printf("[OK] MagSetFullscreenTransform(1.5, 0, 0) returned TRUE\n");
    else
        printf("[FAIL] MagSetFullscreenTransform(1.5, 0, 0) returned FALSE. Error: %lu\n",
               GetLastError());

    Sleep(1000); // Let tester observe the 1.5× zoom

    // E0.2 test: smooth zoom ramp
    printf("[TEST E0.2] Ramping zoom 1.0 -> 3.0 in 0.01 increments...\n");
    for (float z = 1.0f; z <= 3.0f; z += 0.01f)
    {
        MagSetFullscreenTransform(z, 0, 0);
        DwmFlush(); // One frame per increment
    }
    printf("[OK] Ramp complete. Was it visually smooth? (observe)\n");
    Sleep(500);

    // Reset to 1.0×
    MagSetFullscreenTransform(1.0f, 0, 0);
    printf("[OK] Reset to 1.0x\n\n");

    // Install mouse hook for interactive testing (E0.4, E0.5, E0.6)
    g_mouseHook = SetWindowsHookExW(WH_MOUSE_LL, mouseHookProc, nullptr, 0);
    if (!g_mouseHook)
    {
        printf("FATAL: SetWindowsHookExW(WH_MOUSE_LL) failed. Error: %lu\n", GetLastError());
        MagSetFullscreenTransform(1.0f, 0, 0);
        MagUninitialize();
        return 1;
    }
    printf("[OK] Mouse hook installed\n");
    printf("Interactive mode: Hold Win+Scroll to zoom. Ctrl+Q to exit.\n\n");

    // Message pump (required for hooks)
    MSG msg;
    while (g_running && GetMessageW(&msg, nullptr, 0, 0) > 0)
    {
        // Check for Ctrl+Q
        if (GetAsyncKeyState(VK_CONTROL) & 0x8000)
        {
            if (GetAsyncKeyState('Q') & 0x8000)
            {
                printf("Ctrl+Q detected — resetting zoom and exiting.\n");
                g_running = false;
                break;
            }
        }

        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    // Cleanup
    UnhookWindowsHookEx(g_mouseHook);
    MagSetFullscreenTransform(1.0f, 0, 0);
    MagUninitialize();

    printf("\nPhase 0 harness exited cleanly.\n");
    printf("Exit criteria check:\n");
    printf("  E0.1 - Did 1.5x produce visible magnification between 1x and 2x?\n");
    printf("  E0.2 - Was the 0.01-increment ramp visually smooth?\n");
    printf("  E0.3 - Did zoom changes appear within one frame?\n");
    printf("  E0.4 - Did Win+Scroll zoom the screen?\n");
    printf("  E0.5 - Did scroll without Win pass through to foreground app?\n");
    printf("  E0.6 - Did hook work over elevated windows (e.g., admin Task Manager)?\n");

    if (fp)
        fclose(fp);
    FreeConsole();

    return 0;
}

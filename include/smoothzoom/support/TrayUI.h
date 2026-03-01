#pragma once
// =============================================================================
// SmoothZoom — TrayUI
// System tray icon, context menu, settings window. Doc 3 §3.10
// Phase 5C: Full implementation (AC-2.9.10–19, AC-2.8.11)
// =============================================================================

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <windows.h>

namespace SmoothZoom
{

// Forward declarations to minimize header deps
struct SharedState;
class SettingsManager;

class TrayUI
{
public:
    bool create(HINSTANCE hInstance, HWND msgWindow, SharedState& state,
                SettingsManager& settings, const char* configPath);
    void destroy();

    void showSettingsWindow();          // AC-2.8.11: create or bring to foreground
    void onTrayMessage(LPARAM lParam);  // Route WM_TRAYICON notifications
    void requestGracefulExit();         // AC-2.9.16: animate to 1.0× then exit
    bool checkExitPoll();               // Returns true when ready to PostQuitMessage
    bool isExitPending() const;
    HWND settingsHwnd() const;          // For IsDialogMessage in message pump
    void recreateTrayIcon();            // Re-add after Explorer restart
    void showBalloonNotification(const wchar_t* title, const wchar_t* message); // AC-ERR.03

private:
    HINSTANCE hInstance_ = nullptr;
    HWND msgWindow_ = nullptr;
    SharedState* state_ = nullptr;
    SettingsManager* settings_ = nullptr;
    const char* configPath_ = nullptr;

    HWND settingsHwnd_ = nullptr;
    bool exitPending_ = false;
    DWORD exitStartTick_ = 0;

    void addTrayIcon();
    void removeTrayIcon();
    void showContextMenu();
    void createSettingsWindow();
    void populateFromSnapshot();
    void validateAndApply();
    void updateValidationState();
};

} // namespace SmoothZoom

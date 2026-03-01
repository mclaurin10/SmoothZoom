// =============================================================================
// SmoothZoom — TrayUI
// System tray icon, context menu, settings window. Doc 3 §3.10
// Phase 5C: Full implementation (AC-2.9.10–19, AC-2.8.11)
// =============================================================================

#include "smoothzoom/support/TrayUI.h"
#include "smoothzoom/common/AppMessages.h"
#include "smoothzoom/common/SharedState.h"
#include "smoothzoom/common/Types.h"
#include "smoothzoom/support/SettingsManager.h"

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <windows.h>
#include <shellapi.h>
#include <commctrl.h>
#include <cmath>
#include <cstring>
#include <string>

#pragma comment(lib, "Shell32.lib")
#pragma comment(lib, "User32.lib")
#pragma comment(lib, "Advapi32.lib")
#pragma comment(lib, "Comctl32.lib")

namespace SmoothZoom
{

// ── Constants ───────────────────────────────────────────────────────────────
// WM_TRAYICON, WM_GRACEFUL_EXIT, IDM_SETTINGS, IDM_TOGGLE_ZOOM, IDM_EXIT
// are defined in common/AppMessages.h (included above).

// Settings window control IDs
static constexpr int IDC_MODIFIER_COMBO  = 1001;
static constexpr int IDC_TOGGLE1_COMBO   = 1002;
static constexpr int IDC_TOGGLE2_COMBO   = 1003;
static constexpr int IDC_VALIDATION_TEXT  = 1004;
static constexpr int IDC_MIN_ZOOM_EDIT   = 1005;
static constexpr int IDC_MAX_ZOOM_EDIT   = 1006;
static constexpr int IDC_KB_STEP_EDIT    = 1007;
static constexpr int IDC_DEFAULT_ZOOM_EDIT = 1008;
static constexpr int IDC_ANIM_SPEED_COMBO = 1009;
static constexpr int IDC_SMOOTHING_CHECK  = 1010;
static constexpr int IDC_FOLLOW_FOCUS_CHECK = 1011;
static constexpr int IDC_FOLLOW_CARET_CHECK = 1012;
static constexpr int IDC_INVERT_CHECK     = 1013;
static constexpr int IDC_AUTOSTART_CHECK  = 1014;
static constexpr int IDC_START_ZOOMED_CHECK = 1015;
static constexpr int IDC_APPLY_BUTTON     = 1016;
static constexpr int IDC_CLOSE_BUTTON     = 1017;

// Tray icon ID
static constexpr UINT kTrayIconId = 1;

// Graceful exit timeout
static constexpr DWORD kExitTimeoutMs = 5000;
static constexpr float kExitZoomThreshold = 1.005f;

// VK mapping for modifier/toggle combos
static const int kModifierVKs[] = { VK_LWIN, VK_LCONTROL, VK_LMENU, VK_LSHIFT };
static const wchar_t* kModifierNames[] = { L"Win", L"Ctrl", L"Alt", L"Shift" };
static constexpr int kModifierCount = 4;

// Static instance pointer for WndProc routing (only one TrayUI exists)
static TrayUI* s_instance = nullptr;

// ── Auto-Start Registry Helpers (AC-2.9.17) ────────────────────────────────

static const wchar_t* kRunKey = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run";
static const wchar_t* kAppName = L"SmoothZoom";

static void setAutoStart(bool enable)
{
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_SET_VALUE, &hKey) != ERROR_SUCCESS)
        return;

    if (enable)
    {
        wchar_t exePath[MAX_PATH] = {};
        GetModuleFileNameW(nullptr, exePath, MAX_PATH);
        DWORD cbData = static_cast<DWORD>((wcslen(exePath) + 1) * sizeof(wchar_t));
        RegSetValueExW(hKey, kAppName, 0, REG_SZ,
                       reinterpret_cast<const BYTE*>(exePath), cbData);
    }
    else
    {
        RegDeleteValueW(hKey, kAppName);
    }

    RegCloseKey(hKey);
}

static bool isAutoStartEnabled()
{
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_QUERY_VALUE, &hKey) != ERROR_SUCCESS)
        return false;

    DWORD type = 0;
    DWORD size = 0;
    bool exists = (RegQueryValueExW(hKey, kAppName, nullptr, &type, nullptr, &size) == ERROR_SUCCESS);
    RegCloseKey(hKey);
    return exists;
}

// ── VK Helpers ──────────────────────────────────────────────────────────────

static int comboIndexFromVK(int vk)
{
    for (int i = 0; i < kModifierCount; ++i)
    {
        if (kModifierVKs[i] == vk)
            return i;
    }
    return 0; // Default to Win
}

static int vkFromComboIndex(int idx)
{
    if (idx >= 0 && idx < kModifierCount)
        return kModifierVKs[idx];
    return VK_LWIN;
}

// ── Float <-> Edit Helpers ──────────────────────────────────────────────────

static void setEditFloat(HWND hEdit, float val)
{
    wchar_t buf[32];
    swprintf_s(buf, L"%.2f", static_cast<double>(val));
    SetWindowTextW(hEdit, buf);
}

static float getEditFloat(HWND hEdit, float fallback)
{
    wchar_t buf[64] = {};
    GetWindowTextW(hEdit, buf, 64);
    wchar_t* end = nullptr;
    double val = wcstod(buf, &end);
    if (end == buf)
        return fallback;
    return static_cast<float>(val);
}

static void setEditInt(HWND hEdit, int val)
{
    wchar_t buf[32];
    swprintf_s(buf, L"%d", val);
    SetWindowTextW(hEdit, buf);
}

static int getEditInt(HWND hEdit, int fallback)
{
    wchar_t buf[64] = {};
    GetWindowTextW(hEdit, buf, 64);
    wchar_t* end = nullptr;
    long val = wcstol(buf, &end, 10);
    if (end == buf)
        return fallback;
    return static_cast<int>(val);
}

// ── Settings Window WndProc ─────────────────────────────────────────────────

static LRESULT CALLBACK settingsWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_COMMAND:
    {
        int id = LOWORD(wParam);
        int notif = HIWORD(wParam);

        if (id == IDC_APPLY_BUTTON && notif == BN_CLICKED)
        {
            if (s_instance)
                s_instance->validateAndApply();
            return 0;
        }
        if (id == IDC_CLOSE_BUTTON && notif == BN_CLICKED)
        {
            DestroyWindow(hWnd);
            return 0;
        }

        // Live validation on combo/edit changes
        if (notif == CBN_SELCHANGE || notif == EN_CHANGE)
        {
            if (s_instance)
                s_instance->updateValidationState();
            return 0;
        }
        break;
    }

    case WM_CLOSE:
        DestroyWindow(hWnd);
        return 0;

    case WM_DESTROY:
        if (s_instance)
            s_instance->settingsHwnd_ = nullptr;
        return 0;

    default:
        break;
    }

    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

// ── TrayUI Implementation ───────────────────────────────────────────────────

bool TrayUI::create(HINSTANCE hInstance, HWND msgWindow, SharedState& state,
                    SettingsManager& settings, const char* configPath)
{
    hInstance_ = hInstance;
    msgWindow_ = msgWindow;
    state_ = &state;
    settings_ = &settings;
    configPath_ = configPath;
    s_instance = this;

    // Register settings window class
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.lpfnWndProc = settingsWndProc;
    wc.hInstance = hInstance_;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = L"SmoothZoomSettings";
    wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    RegisterClassExW(&wc);

    addTrayIcon();
    return true;
}

void TrayUI::destroy()
{
    if (settingsHwnd_)
    {
        DestroyWindow(settingsHwnd_);
        settingsHwnd_ = nullptr;
    }
    removeTrayIcon();
    s_instance = nullptr;
}

void TrayUI::addTrayIcon()
{
    NOTIFYICONDATAW nid = {};
    nid.cbSize = sizeof(NOTIFYICONDATAW);
    nid.hWnd = msgWindow_;
    nid.uID = kTrayIconId;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_TRAYICON;
    nid.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wcscpy_s(nid.szTip, L"SmoothZoom");
    Shell_NotifyIconW(NIM_ADD, &nid);

    // Set version for modern behavior (balloon, etc.)
    nid.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &nid);
}

void TrayUI::removeTrayIcon()
{
    NOTIFYICONDATAW nid = {};
    nid.cbSize = sizeof(NOTIFYICONDATAW);
    nid.hWnd = msgWindow_;
    nid.uID = kTrayIconId;
    Shell_NotifyIconW(NIM_DELETE, &nid);
}

void TrayUI::recreateTrayIcon()
{
    addTrayIcon();
}

void TrayUI::showBalloonNotification(const wchar_t* title, const wchar_t* message)
{
    if (!msgWindow_)
        return;

    NOTIFYICONDATAW nid = {};
    nid.cbSize = sizeof(NOTIFYICONDATAW);
    nid.hWnd = msgWindow_;
    nid.uID = kTrayIconId;
    nid.uFlags = NIF_INFO;
    nid.dwInfoFlags = NIIF_WARNING;
    wcsncpy_s(nid.szInfoTitle, title ? title : L"", _TRUNCATE);
    wcsncpy_s(nid.szInfo, message ? message : L"", _TRUNCATE);
    Shell_NotifyIconW(NIM_MODIFY, &nid);
}

void TrayUI::onTrayMessage(LPARAM lParam)
{
    UINT event = LOWORD(lParam);

    switch (event)
    {
    case WM_RBUTTONUP:
    case WM_CONTEXTMENU:
        showContextMenu();
        break;

    case WM_LBUTTONDBLCLK:
        showSettingsWindow();
        break;

    default:
        break;
    }
}

void TrayUI::showContextMenu()
{
    HMENU hMenu = CreatePopupMenu();
    if (!hMenu)
        return;

    AppendMenuW(hMenu, MF_STRING, IDM_SETTINGS, L"Settings");
    AppendMenuW(hMenu, MF_STRING, IDM_TOGGLE_ZOOM, L"Toggle Zoom On/Off");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING, IDM_EXIT, L"Exit");

    // Required for tray menu to dismiss when clicking elsewhere
    SetForegroundWindow(msgWindow_);

    POINT pt;
    GetCursorPos(&pt);
    TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, msgWindow_, nullptr);

    // Required after TrackPopupMenu per MSDN
    PostMessageW(msgWindow_, WM_NULL, 0, 0);

    DestroyMenu(hMenu);
}

void TrayUI::showSettingsWindow()
{
    if (settingsHwnd_)
    {
        // Bring existing window to foreground
        SetForegroundWindow(settingsHwnd_);
        return;
    }

    createSettingsWindow();
}

void TrayUI::createSettingsWindow()
{
    // DPI-aware sizing
    int dpi = 96;
    HMODULE hUser32 = GetModuleHandleW(L"user32.dll");
    if (hUser32)
    {
        using GetDpiForSystemFn = UINT(WINAPI*)();
        auto fn = reinterpret_cast<GetDpiForSystemFn>(
            GetProcAddress(hUser32, "GetDpiForSystem"));
        if (fn)
            dpi = static_cast<int>(fn());
    }

    auto scale = [dpi](int v) { return MulDiv(v, dpi, 96); };

    int wndW = scale(480);
    int wndH = scale(580);

    // Center on screen
    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    int x = (screenW - wndW) / 2;
    int y = (screenH - wndH) / 2;

    settingsHwnd_ = CreateWindowExW(
        0, L"SmoothZoomSettings", L"SmoothZoom Settings",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        x, y, wndW, wndH,
        nullptr, nullptr, hInstance_, nullptr);

    if (!settingsHwnd_)
        return;

    // Common layout constants
    int labelX = scale(20);
    int ctrlX  = scale(220);
    int ctrlW  = scale(230);
    int labelW = scale(190);
    int rowH   = scale(24);
    int gap    = scale(40);
    int checkGap = scale(24);

    int curY = scale(20);

    HFONT hFont = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));

    auto createLabel = [&](const wchar_t* text, int yPos) {
        HWND h = CreateWindowExW(0, L"STATIC", text,
            WS_CHILD | WS_VISIBLE,
            labelX, yPos, labelW, rowH,
            settingsHwnd_, nullptr, hInstance_, nullptr);
        SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(hFont), TRUE);
        return h;
    };

    auto createCombo = [&](int id, int yPos) {
        HWND h = CreateWindowExW(0, L"COMBOBOX", nullptr,
            WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_TABSTOP,
            ctrlX, yPos, ctrlW, scale(200),
            settingsHwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
            hInstance_, nullptr);
        SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(hFont), TRUE);
        return h;
    };

    auto createEdit = [&](int id, int yPos) {
        HWND h = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", nullptr,
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | WS_TABSTOP,
            ctrlX, yPos, ctrlW, rowH,
            settingsHwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
            hInstance_, nullptr);
        SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(hFont), TRUE);
        return h;
    };

    auto createCheck = [&](const wchar_t* text, int id, int yPos, bool enabled = true) {
        HWND h = CreateWindowExW(0, L"BUTTON", text,
            WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX | WS_TABSTOP,
            labelX, yPos, labelW + ctrlW, rowH,
            settingsHwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
            hInstance_, nullptr);
        SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(hFont), TRUE);
        if (!enabled)
            EnableWindow(h, FALSE);
        return h;
    };

    // Row 1: Scroll-Gesture Modifier
    createLabel(L"Scroll-Gesture Modifier", curY);
    HWND hModCombo = createCombo(IDC_MODIFIER_COMBO, curY);
    for (int i = 0; i < kModifierCount; ++i)
        SendMessageW(hModCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(kModifierNames[i]));
    curY += gap;

    // Row 2: Toggle Key 1
    createLabel(L"Toggle Key 1", curY);
    HWND hTog1 = createCombo(IDC_TOGGLE1_COMBO, curY);
    for (int i = 0; i < kModifierCount; ++i)
        SendMessageW(hTog1, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(kModifierNames[i]));
    curY += gap;

    // Row 3: Toggle Key 2
    createLabel(L"Toggle Key 2", curY);
    HWND hTog2 = createCombo(IDC_TOGGLE2_COMBO, curY);
    for (int i = 0; i < kModifierCount; ++i)
        SendMessageW(hTog2, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(kModifierNames[i]));
    curY += gap;

    // Validation text (hidden initially)
    HWND hValid = CreateWindowExW(0, L"STATIC", L"",
        WS_CHILD, // Not visible initially
        labelX, curY, labelW + ctrlW, scale(40),
        settingsHwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_VALIDATION_TEXT)),
        hInstance_, nullptr);
    SendMessageW(hValid, WM_SETFONT, reinterpret_cast<WPARAM>(hFont), TRUE);
    curY += scale(32);

    // Row 4: Minimum Zoom
    createLabel(L"Minimum Zoom (1.0\u20135.0)", curY);
    createEdit(IDC_MIN_ZOOM_EDIT, curY);
    curY += gap;

    // Row 5: Maximum Zoom
    createLabel(L"Maximum Zoom (2.0\u201310.0)", curY);
    createEdit(IDC_MAX_ZOOM_EDIT, curY);
    curY += gap;

    // Row 6: Keyboard Step
    createLabel(L"Keyboard Step (5%\u2013100%)", curY);
    createEdit(IDC_KB_STEP_EDIT, curY);
    curY += gap;

    // Row 7: Default Zoom Level
    createLabel(L"Default Zoom Level", curY);
    createEdit(IDC_DEFAULT_ZOOM_EDIT, curY);
    curY += gap;

    // Row 8: Animation Speed
    createLabel(L"Animation Speed", curY);
    HWND hAnim = createCombo(IDC_ANIM_SPEED_COMBO, curY);
    SendMessageW(hAnim, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Slow"));
    SendMessageW(hAnim, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Normal"));
    SendMessageW(hAnim, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Fast"));
    curY += gap;

    // Checkboxes
    createCheck(L"Image Smoothing (Coming soon)", IDC_SMOOTHING_CHECK, curY, false);
    curY += checkGap;

    createCheck(L"Follow Keyboard Focus", IDC_FOLLOW_FOCUS_CHECK, curY);
    curY += checkGap;

    createCheck(L"Follow Text Cursor", IDC_FOLLOW_CARET_CHECK, curY);
    curY += checkGap;

    createCheck(L"Color Inversion", IDC_INVERT_CHECK, curY);
    curY += checkGap;

    createCheck(L"Start with Windows", IDC_AUTOSTART_CHECK, curY);
    curY += checkGap;

    createCheck(L"Start Zoomed", IDC_START_ZOOMED_CHECK, curY);
    curY += scale(36);

    // Apply + Close buttons
    int btnW = scale(90);
    int btnH = scale(28);
    int btnGap = scale(12);
    int btnX = ctrlX + ctrlW - btnW * 2 - btnGap;

    HWND hApply = CreateWindowExW(0, L"BUTTON", L"Apply",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP,
        btnX, curY, btnW, btnH,
        settingsHwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_APPLY_BUTTON)),
        hInstance_, nullptr);
    SendMessageW(hApply, WM_SETFONT, reinterpret_cast<WPARAM>(hFont), TRUE);

    HWND hClose = CreateWindowExW(0, L"BUTTON", L"Close",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP,
        btnX + btnW + btnGap, curY, btnW, btnH,
        settingsHwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_CLOSE_BUTTON)),
        hInstance_, nullptr);
    SendMessageW(hClose, WM_SETFONT, reinterpret_cast<WPARAM>(hFont), TRUE);

    // Populate controls from current settings
    populateFromSnapshot();

    ShowWindow(settingsHwnd_, SW_SHOW);
    UpdateWindow(settingsHwnd_);
}

void TrayUI::populateFromSnapshot()
{
    if (!settingsHwnd_ || !settings_)
        return;

    auto snap = settings_->snapshot();
    if (!snap)
        return;

    // Modifier combo
    SendDlgItemMessageW(settingsHwnd_, IDC_MODIFIER_COMBO, CB_SETCURSEL,
                         comboIndexFromVK(snap->modifierKeyVK), 0);

    // Toggle combos
    SendDlgItemMessageW(settingsHwnd_, IDC_TOGGLE1_COMBO, CB_SETCURSEL,
                         comboIndexFromVK(snap->toggleKey1VK), 0);
    SendDlgItemMessageW(settingsHwnd_, IDC_TOGGLE2_COMBO, CB_SETCURSEL,
                         comboIndexFromVK(snap->toggleKey2VK), 0);

    // Numeric edits
    setEditFloat(GetDlgItem(settingsHwnd_, IDC_MIN_ZOOM_EDIT), snap->minZoom);
    setEditFloat(GetDlgItem(settingsHwnd_, IDC_MAX_ZOOM_EDIT), snap->maxZoom);

    // Keyboard step: stored as fraction, display as percentage
    int stepPct = static_cast<int>(std::round(snap->keyboardZoomStep * 100.0f));
    setEditInt(GetDlgItem(settingsHwnd_, IDC_KB_STEP_EDIT), stepPct);

    setEditFloat(GetDlgItem(settingsHwnd_, IDC_DEFAULT_ZOOM_EDIT), snap->defaultZoomLevel);

    // Animation speed combo
    SendDlgItemMessageW(settingsHwnd_, IDC_ANIM_SPEED_COMBO, CB_SETCURSEL,
                         snap->animationSpeed, 0);

    // Checkboxes
    SendDlgItemMessageW(settingsHwnd_, IDC_SMOOTHING_CHECK, BM_SETCHECK,
                         snap->imageSmoothingEnabled ? BST_CHECKED : BST_UNCHECKED, 0);
    SendDlgItemMessageW(settingsHwnd_, IDC_FOLLOW_FOCUS_CHECK, BM_SETCHECK,
                         snap->followKeyboardFocus ? BST_CHECKED : BST_UNCHECKED, 0);
    SendDlgItemMessageW(settingsHwnd_, IDC_FOLLOW_CARET_CHECK, BM_SETCHECK,
                         snap->followTextCursor ? BST_CHECKED : BST_UNCHECKED, 0);
    SendDlgItemMessageW(settingsHwnd_, IDC_INVERT_CHECK, BM_SETCHECK,
                         snap->colorInversionEnabled ? BST_CHECKED : BST_UNCHECKED, 0);
    SendDlgItemMessageW(settingsHwnd_, IDC_AUTOSTART_CHECK, BM_SETCHECK,
                         isAutoStartEnabled() ? BST_CHECKED : BST_UNCHECKED, 0);
    SendDlgItemMessageW(settingsHwnd_, IDC_START_ZOOMED_CHECK, BM_SETCHECK,
                         snap->startZoomed ? BST_CHECKED : BST_UNCHECKED, 0);

    // Run initial validation
    updateValidationState();
}

void TrayUI::updateValidationState()
{
    if (!settingsHwnd_)
        return;

    HWND hValid = GetDlgItem(settingsHwnd_, IDC_VALIDATION_TEXT);
    if (!hValid)
        return;

    int modIdx = static_cast<int>(SendDlgItemMessageW(settingsHwnd_, IDC_MODIFIER_COMBO, CB_GETCURSEL, 0, 0));
    int tog1Idx = static_cast<int>(SendDlgItemMessageW(settingsHwnd_, IDC_TOGGLE1_COMBO, CB_GETCURSEL, 0, 0));
    int tog2Idx = static_cast<int>(SendDlgItemMessageW(settingsHwnd_, IDC_TOGGLE2_COMBO, CB_GETCURSEL, 0, 0));

    int modVK = vkFromComboIndex(modIdx);
    int tog1VK = vkFromComboIndex(tog1Idx);
    int tog2VK = vkFromComboIndex(tog2Idx);

    std::wstring message;

    // AC-2.9.11: Toggle key conflicts with modifier
    if (tog1VK == modVK || tog2VK == modVK)
    {
        message = L"Toggle keys cannot use the same key as the scroll modifier.";
    }
    // AC-2.1.21: Ctrl as modifier warning
    else if (modVK == VK_LCONTROL)
    {
        message = L"Note: Ctrl+Scroll will be consumed and won't reach apps.";
    }

    if (!message.empty())
    {
        SetWindowTextW(hValid, message.c_str());
        ShowWindow(hValid, SW_SHOW);
    }
    else
    {
        SetWindowTextW(hValid, L"");
        ShowWindow(hValid, SW_HIDE);
    }

    // Disable Apply if there's a conflict (not just warning)
    bool hasConflict = (tog1VK == modVK || tog2VK == modVK);
    HWND hApply = GetDlgItem(settingsHwnd_, IDC_APPLY_BUTTON);
    if (hApply)
        EnableWindow(hApply, hasConflict ? FALSE : TRUE);
}

void TrayUI::validateAndApply()
{
    if (!settingsHwnd_ || !settings_)
        return;

    // Read all values from controls
    int modIdx = static_cast<int>(SendDlgItemMessageW(settingsHwnd_, IDC_MODIFIER_COMBO, CB_GETCURSEL, 0, 0));
    int tog1Idx = static_cast<int>(SendDlgItemMessageW(settingsHwnd_, IDC_TOGGLE1_COMBO, CB_GETCURSEL, 0, 0));
    int tog2Idx = static_cast<int>(SendDlgItemMessageW(settingsHwnd_, IDC_TOGGLE2_COMBO, CB_GETCURSEL, 0, 0));

    int modVK  = vkFromComboIndex(modIdx);
    int tog1VK = vkFromComboIndex(tog1Idx);
    int tog2VK = vkFromComboIndex(tog2Idx);

    // AC-2.9.11: Block apply if toggle key conflicts with modifier
    if (tog1VK == modVK || tog2VK == modVK)
    {
        MessageBoxW(settingsHwnd_,
                    L"Toggle keys cannot use the same key as the scroll modifier.",
                    L"Validation Error", MB_OK | MB_ICONWARNING);
        return;
    }

    float minZoom = getEditFloat(GetDlgItem(settingsHwnd_, IDC_MIN_ZOOM_EDIT), 1.0f);
    float maxZoom = getEditFloat(GetDlgItem(settingsHwnd_, IDC_MAX_ZOOM_EDIT), 10.0f);
    float defaultZoom = getEditFloat(GetDlgItem(settingsHwnd_, IDC_DEFAULT_ZOOM_EDIT), 2.0f);
    int stepPct = getEditInt(GetDlgItem(settingsHwnd_, IDC_KB_STEP_EDIT), 25);
    int animSpeed = static_cast<int>(SendDlgItemMessageW(settingsHwnd_, IDC_ANIM_SPEED_COMBO, CB_GETCURSEL, 0, 0));

    // AC-2.9.10: Min > Max validation
    if (minZoom > maxZoom)
    {
        MessageBoxW(settingsHwnd_,
                    L"Minimum zoom cannot exceed maximum zoom.",
                    L"Validation Error", MB_OK | MB_ICONWARNING);
        return;
    }

    // AC-2.9.12: Clamp keyboard step to [5, 100]
    if (stepPct < 5) stepPct = 5;
    if (stepPct > 100) stepPct = 100;
    setEditInt(GetDlgItem(settingsHwnd_, IDC_KB_STEP_EDIT), stepPct);

    // Clamp numeric ranges
    minZoom = std::max(1.0f, std::min(5.0f, minZoom));
    maxZoom = std::max(2.0f, std::min(10.0f, maxZoom));
    defaultZoom = std::max(minZoom, std::min(maxZoom, defaultZoom));

    // Build snapshot
    SettingsSnapshot snap;
    snap.modifierKeyVK = modVK;
    snap.toggleKey1VK = tog1VK;
    snap.toggleKey2VK = tog2VK;
    snap.minZoom = minZoom;
    snap.maxZoom = maxZoom;
    snap.keyboardZoomStep = static_cast<float>(stepPct) / 100.0f;
    snap.defaultZoomLevel = defaultZoom;
    snap.animationSpeed = animSpeed;
    snap.imageSmoothingEnabled =
        (SendDlgItemMessageW(settingsHwnd_, IDC_SMOOTHING_CHECK, BM_GETCHECK, 0, 0) == BST_CHECKED);
    snap.followKeyboardFocus =
        (SendDlgItemMessageW(settingsHwnd_, IDC_FOLLOW_FOCUS_CHECK, BM_GETCHECK, 0, 0) == BST_CHECKED);
    snap.followTextCursor =
        (SendDlgItemMessageW(settingsHwnd_, IDC_FOLLOW_CARET_CHECK, BM_GETCHECK, 0, 0) == BST_CHECKED);
    snap.colorInversionEnabled =
        (SendDlgItemMessageW(settingsHwnd_, IDC_INVERT_CHECK, BM_GETCHECK, 0, 0) == BST_CHECKED);
    snap.startZoomed =
        (SendDlgItemMessageW(settingsHwnd_, IDC_START_ZOOMED_CHECK, BM_GETCHECK, 0, 0) == BST_CHECKED);
    snap.startWithWindows =
        (SendDlgItemMessageW(settingsHwnd_, IDC_AUTOSTART_CHECK, BM_GETCHECK, 0, 0) == BST_CHECKED);

    // Apply snapshot — observers fire synchronously
    settings_->applySnapshot(snap);

    // Save to disk
    if (configPath_)
        settings_->saveToFile(configPath_);

    // Auto-start registry (AC-2.9.17)
    setAutoStart(snap.startWithWindows);
}

HWND TrayUI::settingsHwnd() const
{
    return settingsHwnd_;
}

bool TrayUI::isExitPending() const
{
    return exitPending_;
}

void TrayUI::requestGracefulExit()
{
    if (exitPending_)
        return;

    if (!state_)
    {
        PostQuitMessage(0);
        return;
    }

    float zoom = state_->currentZoomLevel.load(std::memory_order_relaxed);
    if (zoom <= kExitZoomThreshold)
    {
        // Already at 1.0× — exit immediately
        PostQuitMessage(0);
        return;
    }

    // Animate to 1.0× then exit
    state_->commandQueue.push(ZoomCommand::ResetZoom);
    exitPending_ = true;
    exitStartTick_ = GetTickCount();

    // Start poll timer (50ms)
    if (msgWindow_)
        SetTimer(msgWindow_, 99, 50, nullptr);
}

bool TrayUI::checkExitPoll()
{
    if (!exitPending_ || !state_)
        return false;

    float zoom = state_->currentZoomLevel.load(std::memory_order_relaxed);
    DWORD elapsed = GetTickCount() - exitStartTick_;

    if (zoom <= kExitZoomThreshold || elapsed >= kExitTimeoutMs)
    {
        exitPending_ = false;
        if (msgWindow_)
            KillTimer(msgWindow_, 99);
        return true;
    }

    return false;
}

} // namespace SmoothZoom

// =============================================================================
// SmoothZoom — TrayUI
// System tray icon, context menu, settings window. Doc 3 §3.10
// Phase 5C: Full implementation (AC-2.9.10–19, AC-2.8.11)
// =============================================================================

#include "smoothzoom/support/TrayUI.h"
#include "smoothzoom/common/AppMessages.h"
#include "resource.h"
#include "smoothzoom/common/SharedState.h"
#include "smoothzoom/common/Types.h"
#include "smoothzoom/support/SettingsManager.h"
#include "smoothzoom/input/ModifierUtils.h"

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <windows.h>
#include <shellapi.h>
#include <commctrl.h>
#include <algorithm>
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
// WM_TRAYICON, WM_GRACEFUL_EXIT, IDM_SETTINGS, IDM_TOGGLE_ZOOM, IDM_EXIT,
// IDM_KEYBOARD_SHORTCUTS are defined in common/AppMessages.h (included above).

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
// 1010 was the Image Smoothing checkbox (removed; the setting field stays).
static constexpr int IDC_FOLLOW_FOCUS_CHECK = 1011;
static constexpr int IDC_FOLLOW_CARET_CHECK = 1012;
static constexpr int IDC_INVERT_CHECK     = 1013;
static constexpr int IDC_AUTOSTART_CHECK  = 1014;
static constexpr int IDC_START_ZOOMED_CHECK = 1015;
static constexpr int IDC_REVERSE_SCROLL_CHECK = 1018;    // mouse direction
// 1019 was the shared scroll-sensitivity edit box (now two sliders).
static constexpr int IDC_MOMENTUM_CHECK   = 1020;          // touchpad coast
static constexpr int IDC_STICKY_KEYS_CHECK = 1021;
static constexpr int IDC_SHORTCUTS_CHECK  = 1022;          // mirrors the tray item
static constexpr int IDC_TP_SENS_SLIDER   = 1023;
static constexpr int IDC_TP_SENS_VALUE    = 1024;
static constexpr int IDC_TP_REVERSE_CHECK = 1025;
static constexpr int IDC_MOUSE_SENS_SLIDER = 1026;
static constexpr int IDC_MOUSE_SENS_VALUE = 1027;

// Sensitivity sliders: 0.1x-5.0x in 0.1 steps (SettingsManager's range).
static constexpr int kSensSliderMin = 1, kSensSliderMax = 50;
static constexpr int IDC_APPLY_BUTTON     = 1016;
static constexpr int IDC_CLOSE_BUTTON     = 1017;

// Tray icon ID
static constexpr UINT kTrayIconId = 1;

// Graceful exit timeout
static constexpr DWORD kExitTimeoutMs = 5000;
static constexpr float kExitZoomThreshold = 1.005f;

// Modifier options (Ctrl removed — conflicts with Ctrl+Scroll in browsers/IDEs)
static const int kModifierVKs[] = { VK_LWIN, VK_LMENU, VK_LSHIFT };
static const wchar_t* kModifierNames[] = { L"Win", L"Alt", L"Shift" };
static constexpr int kModifierCount = 3;

// Toggle key options (Ctrl still valid — default toggle is Ctrl+Alt)
static const int kToggleVKs[] = { VK_LWIN, VK_LCONTROL, VK_LMENU, VK_LSHIFT };
static const wchar_t* kToggleNames[] = { L"Win", L"Ctrl", L"Alt", L"Shift" };
static constexpr int kToggleCount = 4;

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
    // Match the whole family, so a config holding VK_RSHIFT / VK_SHIFT / VK_RWIN /
    // VK_RMENU shows the right entry instead of silently becoming "Win" on Apply.
    for (int i = 0; i < kModifierCount; ++i)
    {
        if (isModifierMatch(vk, kModifierVKs[i]))
            return i;
    }
    for (int i = 0; i < kModifierCount; ++i)
    {
        if (kModifierVKs[i] == kDefaultModifierVK)
            return i;
    }
    return 0;
}

static int vkFromComboIndex(int idx)
{
    if (idx >= 0 && idx < kModifierCount)
        return kModifierVKs[idx];
    return kDefaultModifierVK;
}

static int toggleComboIndexFromVK(int vk)
{
    for (int i = 0; i < kToggleCount; ++i)
    {
        if (kToggleVKs[i] == vk)
            return i;
    }
    return 0;
}

static int vkFromToggleComboIndex(int idx)
{
    if (idx >= 0 && idx < kToggleCount)
        return kToggleVKs[idx];
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
    // Reject unparseable input AND non-finite values: wcstod parses "nan"/"inf"
    // successfully (end != buf), but a NaN/Inf zoom bound must never reach the
    // zoom math (R-17). Substituting the finite fallback also keeps the downstream
    // "Min > Max" comparison meaningful (NaN comparisons are always false).
    if (end == buf || !std::isfinite(val))
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

// ── Sensitivity slider helpers (AC-2.9.26) ─────────────────────────────────

static int sliderPosFromSensitivity(float v)
{
    int pos = static_cast<int>(std::lround(v * 10.0f));
    return (std::max)(kSensSliderMin, (std::min)(kSensSliderMax, pos));
}

static float sensitivityFromSlider(HWND hSlider)
{
    LRESULT pos = SendMessageW(hSlider, TBM_GETPOS, 0, 0);
    return static_cast<float>(sliderPosFromSensitivity(static_cast<float>(pos) / 10.0f)) / 10.0f;
}

// The value shown next to a slider, e.g. "1.0×".
static void showSliderValue(HWND hWnd, int sliderId, int valueId)
{
    wchar_t buf[16];
    swprintf_s(buf, L"%.1f\u00D7", static_cast<double>(sensitivityFromSlider(GetDlgItem(hWnd, sliderId))));
    SetDlgItemTextW(hWnd, valueId, buf);
}

// ── Settings Window WndProc ─────────────────────────────────────────────────

LRESULT CALLBACK settingsWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
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

    case WM_HSCROLL:
        // A sensitivity slider moved: update its value label.
        if (reinterpret_cast<HWND>(lParam) == GetDlgItem(hWnd, IDC_TP_SENS_SLIDER))
            showSliderValue(hWnd, IDC_TP_SENS_SLIDER, IDC_TP_SENS_VALUE);
        else if (reinterpret_cast<HWND>(lParam) == GetDlgItem(hWnd, IDC_MOUSE_SENS_SLIDER))
            showSliderValue(hWnd, IDC_MOUSE_SENS_SLIDER, IDC_MOUSE_SENS_VALUE);
        return 0;

    case WM_DPICHANGED:
        if (s_instance)
            s_instance->onSettingsDpiChanged(HIWORD(wParam), reinterpret_cast<const RECT*>(lParam));
        return 0;

    case WM_CLOSE:
        DestroyWindow(hWnd);
        return 0;

    case WM_DESTROY:
        if (s_instance)
            s_instance->settingsHwnd_ = nullptr;
        return 0;

    case WM_NCDESTROY:
        // After the children are gone: the font is no longer in use.
        if (s_instance && s_instance->settingsFont_)
        {
            DeleteObject(s_instance->settingsFont_);
            s_instance->settingsFont_ = nullptr;
        }
        break;

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

    // Trackbars (sensitivity sliders) need the bar classes registered.
    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_STANDARD_CLASSES | ICC_BAR_CLASSES };
    InitCommonControlsEx(&icc);

    // Register settings window class
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.lpfnWndProc = settingsWndProc;
    wc.hInstance = hInstance_;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    // Dialog background: what static, checkbox and group-box controls paint with.
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_3DFACE + 1);
    wc.lpszClassName = L"SmoothZoomSettings";
    wc.hIcon = LoadIconW(hInstance_, MAKEINTRESOURCE(IDI_APP_ICON));
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
    nid.hIcon = LoadIconW(hInstance_, MAKEINTRESOURCE(IDI_TRAY_IDLE));
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
    // AC-2.8.13: checkmark mirrors the live setting each time the menu opens.
    auto snap = settings_ ? settings_->snapshot() : nullptr;
    const bool shortcutsOn = snap && snap->keyboardShortcutsEnabled;
    AppendMenuW(hMenu, MF_STRING | (shortcutsOn ? MF_CHECKED : MF_UNCHECKED),
                IDM_KEYBOARD_SHORTCUTS, L"Keyboard shortcuts");
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

// ── Settings window: font, DPI, layout ─────────────────────────────────────
// The window is laid out in DIPs (96-DPI pixels) and scaled by its own DPI, so
// it is right on whatever monitor it is on and is re-laid out when it moves to
// a monitor with a different scale (WM_DPICHANGED). Text uses the system
// message font (Segoe UI) at that DPI.

static constexpr DWORD kSettingsStyle = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU;

// System message font (NONCLIENTMETRICS::lfMessageFont) sized for dpi.
static HFONT createMessageFont(UINT dpi)
{
    NONCLIENTMETRICSW ncm = {};
    ncm.cbSize = sizeof(ncm);
    if (SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0, dpi))
        return CreateFontIndirectW(&ncm.lfMessageFont);
    LOGFONTW lf = {};
    lf.lfHeight = -MulDiv(9, static_cast<int>(dpi), 72);
    lf.lfWeight = FW_NORMAL;
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfQuality = CLEARTYPE_QUALITY;
    wcscpy_s(lf.lfFaceName, L"Segoe UI");
    return CreateFontIndirectW(&lf);
}

void TrayUI::setSettingsFont(UINT dpi)
{
    HFONT old = settingsFont_;
    settingsFont_ = createMessageFont(dpi);
    if (settingsHwnd_)
    {
        EnumChildWindows(settingsHwnd_, [](HWND child, LPARAM font) -> BOOL {
            SendMessageW(child, WM_SETFONT, static_cast<WPARAM>(font), TRUE);
            return TRUE;
        }, reinterpret_cast<LPARAM>(settingsFont_));
    }
    if (old)
        DeleteObject(old);
}

SIZE TrayUI::layoutSettingsControls(bool create)
{
    const int dpi = static_cast<int>(settingsDpi_);
    auto S = [dpi](int v) { return MulDiv(v, dpi, 96); };
    HWND w = settingsHwnd_;
    int autoId = 1200;   // labels and other id-less statics, numbered in walk order
    int right = 0, bottom = 0;

    // Create or move one control (geometry in DIPs).
    auto place = [&](int id, const wchar_t* cls, const wchar_t* text, DWORD style, DWORD exStyle,
                     int x, int y, int cx, int cy, int visibleCy = -1) -> HWND {
        if (id == 0)
            id = autoId++;
        HWND h = nullptr;
        if (create)
        {
            h = CreateWindowExW(exStyle, cls, text, WS_CHILD | style, 0, 0, 0, 0, w,
                                reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), hInstance_, nullptr);
            if (h)
                SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(settingsFont_), FALSE);
        }
        else
        {
            h = GetDlgItem(w, id);
        }
        if (h)
            SetWindowPos(h, nullptr, S(x), S(y), S(cx), S(cy), SWP_NOZORDER | SWP_NOACTIVATE);
        right = (std::max)(right, x + cx);
        bottom = (std::max)(bottom, y + (visibleCy >= 0 ? visibleCy : cy));
        return h;
    };

    // Two columns of sections (AC-2.9.24). Left: Activation, Zoom. Right:
    // Touchpad, Mouse, Tracking, Display, Startup. Each field's label is the
    // static created just before it, which is where its accessible name comes
    // from (AC-2.9.28).
    const int margin = 12, colW = 300, colGap = 12;
    const int padX = 12, headerH = 22, groupPadBottom = 8, groupGap = 10;
    const int rowH = 30, ctrlH = 23, checkH = 24;
    const int labelW = 150;
    const int innerW = colW - 2 * padX;
    const int ctrlW = innerW - labelW;
    const int editW = 70;
    const int valueW = 44;
    const int colX[2] = { margin, margin + colW + colGap };
    int colY[2] = { margin, margin };
    int col = 0, groupTop = 0;
    HWND group = nullptr;

    auto beginGroup = [&](int c, const wchar_t* title) {
        col = c;
        groupTop = colY[col];
        // The group box is created before its contents (it sits under them);
        // its height is set in endGroup once they are laid out.
        group = place(0, L"BUTTON", title, WS_VISIBLE | BS_GROUPBOX, 0, colX[col], groupTop, colW, headerH);
        colY[col] += headerH;
    };
    auto endGroup = [&]() {
        colY[col] += groupPadBottom;
        if (group)
            SetWindowPos(group, nullptr, S(colX[col]), S(groupTop), S(colW), S(colY[col] - groupTop),
                         SWP_NOZORDER | SWP_NOACTIVATE);
        bottom = (std::max)(bottom, colY[col]);
        colY[col] += groupGap;
    };
    auto x0 = [&]() { return colX[col] + padX; };
    auto label = [&](const wchar_t* text, int width) {
        place(0, L"STATIC", text, WS_VISIBLE, 0, x0(), colY[col] + 4, width, ctrlH - 4);
    };
    auto comboRow = [&](const wchar_t* text, int id, const wchar_t* const* items, int count) {
        label(text, labelW);
        HWND h = place(id, L"COMBOBOX", nullptr, WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, 0,
                       x0() + labelW, colY[col], ctrlW, 200, ctrlH);
        if (create && h)
            for (int i = 0; i < count; ++i)
                SendMessageW(h, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(items[i]));
        colY[col] += rowH;
    };
    auto editRow = [&](const wchar_t* text, int id) {
        label(text, labelW);
        place(id, L"EDIT", nullptr, WS_VISIBLE | ES_AUTOHSCROLL | WS_TABSTOP, WS_EX_CLIENTEDGE,
              x0() + labelW, colY[col], editW, ctrlH);
        colY[col] += rowH;
    };
    auto checkRow = [&](const wchar_t* text, int id) {
        place(id, L"BUTTON", text, WS_VISIBLE | BS_AUTOCHECKBOX | WS_TABSTOP, 0,
              x0(), colY[col] + 2, innerW, checkH - 4);
        colY[col] += checkH;
    };
    // Label on its own line, then a full-width slider with its value on the right.
    auto sliderRow = [&](const wchar_t* text, int sliderId, int valueId) {
        place(0, L"STATIC", text, WS_VISIBLE, 0, x0(), colY[col] + 2, innerW, 18);
        colY[col] += 20;
        HWND h = place(sliderId, TRACKBAR_CLASSW, nullptr,
                       WS_VISIBLE | WS_TABSTOP | TBS_HORZ | TBS_AUTOTICKS | TBS_BOTTOM | TBS_FIXEDLENGTH, 0,
                       x0() - 4, colY[col], innerW - valueW + 4, 28);
        if (h)
        {
            if (create)
            {
                SendMessageW(h, TBM_SETRANGEMIN, FALSE, kSensSliderMin);
                SendMessageW(h, TBM_SETRANGEMAX, FALSE, kSensSliderMax);
                SendMessageW(h, TBM_SETTICFREQ, 10, 0);   // ticks at 1x, 2x, ... 5x
                SendMessageW(h, TBM_SETLINESIZE, 0, 1);
                SendMessageW(h, TBM_SETPAGESIZE, 0, 5);
            }
            SendMessageW(h, TBM_SETTHUMBLENGTH, static_cast<WPARAM>(S(18)), 0);
        }
        place(valueId, L"STATIC", L"", WS_VISIBLE | SS_RIGHT, 0,
              x0() + innerW - valueW, colY[col] + 5, valueW, 18);
        colY[col] += 32;
    };

    static const wchar_t* const kSpeeds[] = { L"Slow", L"Normal", L"Fast" };

    // ── Left column ──
    beginGroup(0, L"Activation");
    comboRow(L"Modifier key", IDC_MODIFIER_COMBO, kModifierNames, kModifierCount);
    checkRow(L"Keyboard shortcuts", IDC_SHORTCUTS_CHECK);                        // AC-2.8.13, AC-2.9.27
    checkRow(L"Block the Sticky Keys prompt (5× Shift)", IDC_STICKY_KEYS_CHECK);  // AC-2.1.23
    comboRow(L"Toggle key 1", IDC_TOGGLE1_COMBO, kToggleNames, kToggleCount);
    comboRow(L"Toggle key 2", IDC_TOGGLE2_COMBO, kToggleNames, kToggleCount);
    // Conflict message (AC-2.9.11), shown only when there is one.
    place(IDC_VALIDATION_TEXT, L"STATIC", L"", 0, 0, x0(), colY[col] + 2, innerW, 18);
    colY[col] += 22;
    endGroup();

    beginGroup(0, L"Zoom");
    editRow(L"Minimum zoom (1–5×)", IDC_MIN_ZOOM_EDIT);
    editRow(L"Maximum zoom (2–10×)", IDC_MAX_ZOOM_EDIT);
    editRow(L"Default zoom level", IDC_DEFAULT_ZOOM_EDIT);
    editRow(L"Keyboard step (5–100%)", IDC_KB_STEP_EDIT);
    comboRow(L"Animation speed", IDC_ANIM_SPEED_COMBO, kSpeeds, 3);
    checkRow(L"Start zoomed", IDC_START_ZOOMED_CHECK);
    endGroup();

    // ── Right column ──
    beginGroup(1, L"Touchpad");
    sliderRow(L"Touchpad sensitivity", IDC_TP_SENS_SLIDER, IDC_TP_SENS_VALUE);    // AC-2.9.20
    checkRow(L"Reverse direction (fingers up zoom out)", IDC_TP_REVERSE_CHECK);   // AC-2.9.21
    checkRow(L"Keep zooming after a flick", IDC_MOMENTUM_CHECK);                  // AC-2.1.28-29
    endGroup();

    beginGroup(1, L"Mouse");
    sliderRow(L"Mouse wheel sensitivity", IDC_MOUSE_SENS_SLIDER, IDC_MOUSE_SENS_VALUE);
    checkRow(L"Reverse direction (wheel toward you zooms in)", IDC_REVERSE_SCROLL_CHECK);
    endGroup();

    beginGroup(1, L"Tracking");
    checkRow(L"Follow keyboard focus", IDC_FOLLOW_FOCUS_CHECK);
    checkRow(L"Follow text cursor", IDC_FOLLOW_CARET_CHECK);
    endGroup();

    beginGroup(1, L"Display");
    checkRow(L"Invert colors", IDC_INVERT_CHECK);
    endGroup();

    beginGroup(1, L"Startup");
    checkRow(L"Start with Windows", IDC_AUTOSTART_CHECK);
    endGroup();

    // Apply + Close, bottom right under both columns.
    const int btnW = 90, btnH = 28, btnGap = 10;
    const int btnY = (std::max)(colY[0], colY[1]) - groupGap + 12;
    const int btnRight = colX[1] + colW;
    place(IDC_APPLY_BUTTON, L"BUTTON", L"Apply", WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP, 0,
          btnRight - 2 * btnW - btnGap, btnY, btnW, btnH);
    place(IDC_CLOSE_BUTTON, L"BUTTON", L"Close", WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP, 0,
          btnRight - btnW, btnY, btnW, btnH);

    return SIZE{ S(right + margin), S(bottom + margin) };
}

void TrayUI::createSettingsWindow()
{
    // Open on the monitor under the pointer (the tray, or wherever Win+Ctrl+M
    // was pressed), inside its work area.
    POINT pt = {};
    GetCursorPos(&pt);
    MONITORINFO mi = {};
    mi.cbSize = sizeof(mi);
    GetMonitorInfoW(MonitorFromPoint(pt, MONITOR_DEFAULTTOPRIMARY), &mi);
    const RECT wa = mi.rcWork;

    // Created hidden on that monitor first so it has that monitor's DPI.
    settingsHwnd_ = CreateWindowExW(
        0, L"SmoothZoomSettings", L"SmoothZoom Settings", kSettingsStyle,
        wa.left, wa.top, 100, 100,
        nullptr, nullptr, hInstance_, nullptr);
    if (!settingsHwnd_)
        return;

    settingsDpi_ = GetDpiForWindow(settingsHwnd_);
    if (settingsDpi_ == 0)
        settingsDpi_ = 96;
    setSettingsFont(settingsDpi_);
    const SIZE client = layoutSettingsControls(true);

    RECT rc = { 0, 0, client.cx, client.cy };
    AdjustWindowRectExForDpi(&rc, kSettingsStyle, FALSE, 0, settingsDpi_);
    const int ww = rc.right - rc.left, wh = rc.bottom - rc.top;
    const int x = wa.left + (std::max)(0, static_cast<int>((wa.right - wa.left - ww) / 2));
    const int y = wa.top + (std::max)(0, static_cast<int>((wa.bottom - wa.top - wh) / 2));
    SetWindowPos(settingsHwnd_, nullptr, x, y, ww, wh, SWP_NOZORDER | SWP_NOACTIVATE);

    // Populate controls from current settings
    populateFromSnapshot();

    ShowWindow(settingsHwnd_, SW_SHOW);
    UpdateWindow(settingsHwnd_);
}

void TrayUI::onSettingsDpiChanged(UINT dpi, const RECT* suggested)
{
    // Moved to a monitor with a different scale (or the scale changed): new
    // font, controls re-laid out for the new DPI, and the window takes the
    // rectangle Windows suggests (keeps it under the pointer while dragging).
    settingsDpi_ = dpi ? dpi : 96;
    setSettingsFont(settingsDpi_);
    layoutSettingsControls(false);
    if (suggested)
        SetWindowPos(settingsHwnd_, nullptr, suggested->left, suggested->top,
                     suggested->right - suggested->left, suggested->bottom - suggested->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
    InvalidateRect(settingsHwnd_, nullptr, TRUE);
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
                         toggleComboIndexFromVK(snap->toggleKey1VK), 0);
    SendDlgItemMessageW(settingsHwnd_, IDC_TOGGLE2_COMBO, CB_SETCURSEL,
                         toggleComboIndexFromVK(snap->toggleKey2VK), 0);

    // Numeric edits
    setEditFloat(GetDlgItem(settingsHwnd_, IDC_MIN_ZOOM_EDIT), snap->minZoom);
    setEditFloat(GetDlgItem(settingsHwnd_, IDC_MAX_ZOOM_EDIT), snap->maxZoom);

    // Keyboard step: stored as fraction, display as percentage
    int stepPct = static_cast<int>(std::round(snap->keyboardZoomStep * 100.0f));
    setEditInt(GetDlgItem(settingsHwnd_, IDC_KB_STEP_EDIT), stepPct);

    setEditFloat(GetDlgItem(settingsHwnd_, IDC_DEFAULT_ZOOM_EDIT), snap->defaultZoomLevel);

    // Sensitivity sliders (AC-2.9.20, AC-2.9.26)
    SendDlgItemMessageW(settingsHwnd_, IDC_TP_SENS_SLIDER, TBM_SETPOS, TRUE,
                        sliderPosFromSensitivity(snap->touchpadSensitivity));
    SendDlgItemMessageW(settingsHwnd_, IDC_MOUSE_SENS_SLIDER, TBM_SETPOS, TRUE,
                        sliderPosFromSensitivity(snap->scrollSensitivity));
    showSliderValue(settingsHwnd_, IDC_TP_SENS_SLIDER, IDC_TP_SENS_VALUE);
    showSliderValue(settingsHwnd_, IDC_MOUSE_SENS_SLIDER, IDC_MOUSE_SENS_VALUE);

    // Animation speed combo
    SendDlgItemMessageW(settingsHwnd_, IDC_ANIM_SPEED_COMBO, CB_SETCURSEL,
                         snap->animationSpeed, 0);

    // Checkboxes
    SendDlgItemMessageW(settingsHwnd_, IDC_SHORTCUTS_CHECK, BM_SETCHECK,
                         snap->keyboardShortcutsEnabled ? BST_CHECKED : BST_UNCHECKED, 0);
    SendDlgItemMessageW(settingsHwnd_, IDC_TP_REVERSE_CHECK, BM_SETCHECK,
                         snap->touchpadReverseScrollDirection ? BST_CHECKED : BST_UNCHECKED, 0);
    SendDlgItemMessageW(settingsHwnd_, IDC_FOLLOW_FOCUS_CHECK, BM_SETCHECK,
                         snap->followKeyboardFocus ? BST_CHECKED : BST_UNCHECKED, 0);
    SendDlgItemMessageW(settingsHwnd_, IDC_FOLLOW_CARET_CHECK, BM_SETCHECK,
                         snap->followTextCursor ? BST_CHECKED : BST_UNCHECKED, 0);
    SendDlgItemMessageW(settingsHwnd_, IDC_INVERT_CHECK, BM_SETCHECK,
                         snap->colorInversionEnabled ? BST_CHECKED : BST_UNCHECKED, 0);
    SendDlgItemMessageW(settingsHwnd_, IDC_REVERSE_SCROLL_CHECK, BM_SETCHECK,
                         snap->reverseScrollDirection ? BST_CHECKED : BST_UNCHECKED, 0);
    SendDlgItemMessageW(settingsHwnd_, IDC_MOMENTUM_CHECK, BM_SETCHECK,
                         snap->momentumZoom ? BST_CHECKED : BST_UNCHECKED, 0);
    SendDlgItemMessageW(settingsHwnd_, IDC_STICKY_KEYS_CHECK, BM_SETCHECK,
                         snap->blockStickyKeysShortcut ? BST_CHECKED : BST_UNCHECKED, 0);
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
    int tog1VK = vkFromToggleComboIndex(tog1Idx);
    int tog2VK = vkFromToggleComboIndex(tog2Idx);

    std::wstring message;

    // AC-2.9.11: Toggle key conflicts with modifier
    if (tog1VK == modVK || tog2VK == modVK)
    {
        message = L"A toggle key can't be the modifier key.";
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
    int tog1VK = vkFromToggleComboIndex(tog1Idx);
    int tog2VK = vkFromToggleComboIndex(tog2Idx);

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
    // Sliders hold only valid values (0.1-5.0, AC-2.9.20).
    float mouseSens = sensitivityFromSlider(GetDlgItem(settingsHwnd_, IDC_MOUSE_SENS_SLIDER));
    float touchpadSens = sensitivityFromSlider(GetDlgItem(settingsHwnd_, IDC_TP_SENS_SLIDER));

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
    minZoom = (std::max)(1.0f, (std::min)(5.0f, minZoom));
    maxZoom = (std::max)(2.0f, (std::min)(10.0f, maxZoom));
    defaultZoom = (std::max)(minZoom, (std::min)(maxZoom, defaultZoom));

    // Build snapshot from the CURRENT one so fields that have no control in
    // this window (e.g. logLevel — the on-device debug knob; see
    // SettingsManager.h) are preserved instead of being reset to their defaults
    // and persisted on every Apply. Mirrors the color-inversion persist path in
    // main.cpp, which copies the live snapshot before mutating a single field.
    auto current = settings_->snapshot();
    SettingsSnapshot snap = current ? *current : SettingsSnapshot{};
    snap.modifierKeyVK = modVK;
    snap.toggleKey1VK = tog1VK;
    snap.toggleKey2VK = tog2VK;
    snap.minZoom = minZoom;
    snap.maxZoom = maxZoom;
    snap.keyboardZoomStep = static_cast<float>(stepPct) / 100.0f;
    snap.defaultZoomLevel = defaultZoom;
    snap.animationSpeed = animSpeed;
    // imageSmoothingEnabled has no control (always on, API limitation); kept as is.
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
    snap.reverseScrollDirection =
        (SendDlgItemMessageW(settingsHwnd_, IDC_REVERSE_SCROLL_CHECK, BM_GETCHECK, 0, 0) == BST_CHECKED);
    snap.scrollSensitivity = mouseSens;
    snap.touchpadReverseScrollDirection =
        (SendDlgItemMessageW(settingsHwnd_, IDC_TP_REVERSE_CHECK, BM_GETCHECK, 0, 0) == BST_CHECKED);
    snap.touchpadSensitivity = touchpadSens;
    snap.momentumZoom =
        (SendDlgItemMessageW(settingsHwnd_, IDC_MOMENTUM_CHECK, BM_GETCHECK, 0, 0) == BST_CHECKED);
    snap.keyboardShortcutsEnabled =
        (SendDlgItemMessageW(settingsHwnd_, IDC_SHORTCUTS_CHECK, BM_GETCHECK, 0, 0) == BST_CHECKED);
    snap.blockStickyKeysShortcut =
        (SendDlgItemMessageW(settingsHwnd_, IDC_STICKY_KEYS_CHECK, BM_GETCHECK, 0, 0) == BST_CHECKED);

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

void TrayUI::toggleKeyboardShortcuts()
{
    if (!settings_)
        return;

    // Copy the live snapshot and flip one field (same pattern as
    // validateAndApply and main.cpp's inversion persist path). applySnapshot
    // notifies InputInterceptor's observer synchronously on this (main) thread,
    // so the very next key event sees the new value — no restart (AC-2.8.15).
    auto current = settings_->snapshot();
    SettingsSnapshot snap = current ? *current : SettingsSnapshot{};
    snap.keyboardShortcutsEnabled = !snap.keyboardShortcutsEnabled;
    settings_->applySnapshot(snap);

    // Keep an open settings window's checkbox in step (AC-2.9.27). Only that
    // control: unapplied edits in the window stay as they are.
    if (settingsHwnd_)
        SendDlgItemMessageW(settingsHwnd_, IDC_SHORTCUTS_CHECK, BM_SETCHECK,
                            snap.keyboardShortcutsEnabled ? BST_CHECKED : BST_UNCHECKED, 0);

    if (configPath_)
        settings_->saveToFile(configPath_);
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

void TrayUI::updateTrayIcon(bool isZoomed)
{
    NOTIFYICONDATAW nid = {};
    nid.cbSize = sizeof(NOTIFYICONDATAW);
    nid.hWnd = msgWindow_;
    nid.uID = kTrayIconId;
    nid.uFlags = NIF_ICON | NIF_TIP;
    nid.hIcon = LoadIconW(hInstance_,
        MAKEINTRESOURCE(isZoomed ? IDI_TRAY_ACTIVE : IDI_TRAY_IDLE));
    wcscpy_s(nid.szTip, isZoomed ? L"SmoothZoom (Zoomed)" : L"SmoothZoom");
    Shell_NotifyIconW(NIM_MODIFY, &nid);
}

} // namespace SmoothZoom

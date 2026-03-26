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
// Exception: direct Magnification API include for crash handler (Fix 1).
// Normally only MagBridge.cpp includes this (R-01 isolation rule), but the
// crash handler cannot safely construct a MagBridge on an arbitrary thread.
#include <magnification.h>
#pragma comment(lib, "Magnification.lib")

#include "smoothzoom/common/AppMessages.h"
#include "resource.h"
#include "smoothzoom/common/SharedState.h"
#include "smoothzoom/input/InputInterceptor.h"
#include "smoothzoom/input/FocusMonitor.h"
#include "smoothzoom/input/CaretMonitor.h"
#include "smoothzoom/logic/RenderLoop.h"
#include "smoothzoom/output/MagBridge.h"
#include "smoothzoom/support/SettingsManager.h"
#include "smoothzoom/support/TrayUI.h"
#include "smoothzoom/support/Logger.h"
#include "smoothzoom/input/ModifierUtils.h"

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <tlhelp32.h>
#include <wtsapi32.h>

#pragma comment(lib, "Wtsapi32.lib")

// Precision Touchpad HID report parsing (device-independent via HidP_* APIs)
extern "C" {
#include <hidusage.h>
#include <hidpi.h>
}
#pragma comment(lib, "Hid.lib")

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

// Tray icon state polling timer (zoom idle/active)
static constexpr UINT_PTR kTrayIconTimerId = 2;
static constexpr UINT kTrayIconIntervalMs = 250;

// Hook failure notification flag (AC-ERR.03) — suppress repeated balloons
static bool s_hookFailureNotified = false;

// Session lock state (AC-ERR.04) — suppress hook-failure balloons during lock/UAC
static bool s_sessionLocked = false;

// Tray icon zoom-state hysteresis — only update icon on transitions
static bool s_trayIconShowsZoomed = false;

// Sentinel path for dirty-shutdown detection (R-14, E6.11)
static std::filesystem::path s_sentinelPath;

// ── Precision Touchpad (PTP) HID Parsing State ──────────────────────────────
// Parses raw HID contact reports to detect two-finger vertical pan gestures.
// Needed because PTP drivers deliver scroll via WM_MOUSEWHEEL directly to the
// foreground window, bypassing LL mouse hooks for Desktop/Edge.

struct PtpContactSlot {
    USHORT linkCollection;  // HID link collection index for this contact
};

struct PtpDeviceInfo {
    PHIDP_PREPARSED_DATA preparsedData = nullptr;
    bool valid = false;
    USHORT contactCountLC = 0;     // Link collection for Contact Count (0x0D:0x54)
    PtpContactSlot slots[5] = {};  // Contact slots (max 5 per PTP spec)
    int numSlots = 0;
};

struct PtpContactState {
    bool active = false;
    LONG y = 0;
    LONG prevY = 0;
    bool hasPrev = false;  // true if previous Y is valid (contact was active last frame)
};

static PtpDeviceInfo s_ptpDevice = {};
static HANDLE s_ptpDeviceHandle = nullptr;
static PtpContactState s_ptpContacts[5] = {};
static int32_t s_ptpScrollRemainder = 0;
// Device units of Y movement per WHEEL_DELTA notch (120). Tunable.
static constexpr int32_t kPtpUnitsPerNotch = 25;

// Windows touchpad "natural scrolling" direction setting.
// When true, the OS flips WM_MOUSEWHEEL deltas for touchpad gestures before
// they reach the LL hook — but raw HID reports are unaffected. The PTP path
// must compensate to match the LL hook's (already-flipped) convention.
// Registry: HKCU\SOFTWARE\Microsoft\Windows\CurrentVersion\PrecisionTouchPad
//   ScrollDirection = 0 → natural (reverse), 1 or absent → traditional.
static bool s_ptpNaturalScrolling = false;

static bool queryTouchpadNaturalScrolling()
{
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
                      L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\PrecisionTouchPad",
                      0, KEY_READ, &hKey) != ERROR_SUCCESS)
        return false;

    DWORD value = 1;
    DWORD size = sizeof(value);
    DWORD type = 0;
    bool natural = false;
    if (RegQueryValueExW(hKey, L"ScrollDirection", nullptr, &type,
                         reinterpret_cast<LPBYTE>(&value), &size) == ERROR_SUCCESS
        && type == REG_DWORD)
    {
        natural = (value == 0);
    }
    RegCloseKey(hKey);
    return natural;
}

// ── Dirty-Shutdown Sentinel Helpers (R-14) ──────────────────────────────────

static std::filesystem::path getSentinelPath()
{
    std::string configPath = SmoothZoom::SettingsManager::getDefaultConfigPath();
    if (configPath.empty())
        return {};
    return std::filesystem::path(configPath).parent_path() / ".running";
}

static bool writeSentinel(const std::filesystem::path& path)
{
    if (path.empty())
        return false;
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec)
        return false;
    std::ofstream ofs(path, std::ios::trunc);
    if (!ofs)
        return false;
    ofs << GetCurrentProcessId();
    return true;
}

static void removeSentinel(const std::filesystem::path& path)
{
    if (path.empty())
        return;
    std::error_code ec;
    std::filesystem::remove(path, ec);
}

static bool sentinelExists(const std::filesystem::path& path)
{
    if (path.empty())
        return false;
    std::error_code ec;
    return std::filesystem::exists(path, ec);
}

// ── Conflict Detection Helpers (AC-ERR.01, E6.8) ────────────────────────────

static DWORD findMagnifyExe()
{
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE)
        return 0;

    PROCESSENTRY32W pe = {};
    pe.dwSize = sizeof(PROCESSENTRY32W);

    DWORD pid = 0;
    if (Process32FirstW(hSnap, &pe))
    {
        do
        {
            if (_wcsicmp(pe.szExeFile, L"Magnify.exe") == 0)
            {
                pid = pe.th32ProcessID;
                break;
            }
        } while (Process32NextW(hSnap, &pe));
    }
    CloseHandle(hSnap);
    return pid;
}

static bool terminateMagnifyExe(DWORD pid)
{
    HANDLE hProc = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
    if (!hProc)
        return false;
    BOOL ok = TerminateProcess(hProc, 0);
    CloseHandle(hProc);
    return ok != FALSE;
}

using SmoothZoom::WM_OPEN_SETTINGS;
using SmoothZoom::WM_TRAYICON;
using SmoothZoom::WM_GRACEFUL_EXIT;
using SmoothZoom::WM_UPDATE_TRAY_ICON;
using SmoothZoom::IDM_SETTINGS;
using SmoothZoom::IDM_TOGGLE_ZOOM;
using SmoothZoom::IDM_EXIT;
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

// Crash handler (R-14): reset magnification on unhandled exception.
// IMPORTANT: Cannot use MagBridge here — it allocates on the heap and may run on
// a thread different from the one that called MagInitialize (thread affinity).
// Direct Mag* calls are the only safe option in an exception filter.
static LONG WINAPI crashHandler(EXCEPTION_POINTERS* /*exInfo*/)
{
    // Best-effort zoom reset — direct API calls, no heap allocation.
    // MagSetFullscreenTransform has process-global effect regardless of thread.
    MagSetFullscreenTransform(1.0f, 0, 0);

    // Best-effort input transform reset — may fail if Mag state is corrupted.
    __try
    {
        RECT r = {0, 0, GetSystemMetrics(SM_CXVIRTUALSCREEN),
                         GetSystemMetrics(SM_CYVIRTUALSCREEN)};
        MagSetInputTransform(FALSE, &r, &r);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        // Swallow — we're already in a crash handler
    }

    // Best-effort sentinel removal (may fail if heap is corrupted — next launch catches it)
    removeSentinel(s_sentinelPath);

    return EXCEPTION_CONTINUE_SEARCH;
}

// ── PTP HID Device Initialization ────────────────────────────────────────────
// Called once per device when the first HID report arrives. Parses the device's
// HID report descriptor to discover contact slot link collections.

static bool initPtpDevice(HANDLE hDevice)
{
    // One-shot guard: log detailed diagnostics only on first failure per device
    static bool s_ptpInitDiagLogged = false;

    UINT ppSize = 0;
    if (GetRawInputDeviceInfoW(hDevice, RIDI_PREPARSEDDATA, nullptr, &ppSize) != 0)
    {
        if (!s_ptpInitDiagLogged)
            SZ_LOG_WARN("PTP", L"initPtpDevice: GetRawInputDeviceInfoW size query failed (GetLastError=%lu)", GetLastError());
        s_ptpInitDiagLogged = true;
        return false;
    }
    if (ppSize == 0 || ppSize > 65536)
    {
        if (!s_ptpInitDiagLogged)
            SZ_LOG_WARN("PTP", L"initPtpDevice: preparsed data size out of range (ppSize=%u)", ppSize);
        s_ptpInitDiagLogged = true;
        return false;
    }

    auto* ppd = static_cast<PHIDP_PREPARSED_DATA>(
        HeapAlloc(GetProcessHeap(), 0, ppSize));
    if (!ppd)
    {
        if (!s_ptpInitDiagLogged)
            SZ_LOG_WARN("PTP", L"initPtpDevice: HeapAlloc for preparsed data failed (size=%u)", ppSize);
        s_ptpInitDiagLogged = true;
        return false;
    }

    if (GetRawInputDeviceInfoW(hDevice, RIDI_PREPARSEDDATA, ppd, &ppSize) == UINT(-1))
    {
        if (!s_ptpInitDiagLogged)
            SZ_LOG_WARN("PTP", L"initPtpDevice: GetRawInputDeviceInfoW data retrieval failed (GetLastError=%lu)", GetLastError());
        s_ptpInitDiagLogged = true;
        HeapFree(GetProcessHeap(), 0, ppd);
        return false;
    }

    HIDP_CAPS caps = {};
    NTSTATUS capsStatus = HidP_GetCaps(ppd, &caps);
    if (capsStatus != HIDP_STATUS_SUCCESS)
    {
        if (!s_ptpInitDiagLogged)
            SZ_LOG_WARN("PTP", L"initPtpDevice: HidP_GetCaps failed (NTSTATUS=0x%08lX)", static_cast<unsigned long>(capsStatus));
        s_ptpInitDiagLogged = true;
        HeapFree(GetProcessHeap(), 0, ppd);
        return false;
    }

    // Enumerate input value caps to find Contact Count and Contact ID locations
    USHORT numValCaps = caps.NumberInputValueCaps;
    if (numValCaps == 0 || numValCaps > 256)
    {
        if (!s_ptpInitDiagLogged)
            SZ_LOG_WARN("PTP", L"initPtpDevice: NumberInputValueCaps out of range (numValCaps=%u, UsagePage=0x%04X, Usage=0x%04X)",
                        numValCaps, caps.UsagePage, caps.Usage);
        s_ptpInitDiagLogged = true;
        HeapFree(GetProcessHeap(), 0, ppd);
        return false;
    }

    auto* valCaps = static_cast<HIDP_VALUE_CAPS*>(
        HeapAlloc(GetProcessHeap(), 0, numValCaps * sizeof(HIDP_VALUE_CAPS)));
    if (!valCaps)
    {
        if (!s_ptpInitDiagLogged)
            SZ_LOG_WARN("PTP", L"initPtpDevice: HeapAlloc for value caps failed (count=%u)", numValCaps);
        s_ptpInitDiagLogged = true;
        HeapFree(GetProcessHeap(), 0, ppd);
        return false;
    }

    NTSTATUS valStatus = HidP_GetValueCaps(HidP_Input, valCaps, &numValCaps, ppd);
    if (valStatus != HIDP_STATUS_SUCCESS)
    {
        if (!s_ptpInitDiagLogged)
            SZ_LOG_WARN("PTP", L"initPtpDevice: HidP_GetValueCaps failed (NTSTATUS=0x%08lX)", static_cast<unsigned long>(valStatus));
        s_ptpInitDiagLogged = true;
        HeapFree(GetProcessHeap(), 0, valCaps);
        HeapFree(GetProcessHeap(), 0, ppd);
        return false;
    }

    if (!s_ptpInitDiagLogged)
    {
        SZ_LOG_INFO("PTP", L"initPtpDevice: HID caps — UsagePage=0x%04X, Usage=0x%04X, InputValueCaps=%u, InputButtonCaps=%u",
                    caps.UsagePage, caps.Usage, caps.NumberInputValueCaps, caps.NumberInputButtonCaps);
    }

    bool foundCC = false;
    USHORT contactCountLC = 0;
    int numSlots = 0;
    USHORT slotLCs[5] = {};

    for (USHORT i = 0; i < numValCaps; i++)
    {
        USAGE usage = valCaps[i].IsRange
            ? valCaps[i].Range.UsageMin
            : valCaps[i].NotRange.Usage;
        USAGE page = valCaps[i].UsagePage;

        // Log each value cap on first attempt for diagnostics
        if (!s_ptpInitDiagLogged)
        {
            SZ_LOG_INFO("PTP", L"  ValCap[%u]: UsagePage=0x%04X, Usage=0x%04X, LinkCollection=%u, IsRange=%d, BitSize=%u",
                        i, page, usage, valCaps[i].LinkCollection, valCaps[i].IsRange, valCaps[i].BitSize);
        }

        if (page == 0x0D && usage == 0x54) // Contact Count
        {
            contactCountLC = valCaps[i].LinkCollection;
            foundCC = true;
        }
        if (page == 0x0D && usage == 0x51) // Contact ID — one per contact slot
        {
            if (numSlots < 5)
                slotLCs[numSlots++] = valCaps[i].LinkCollection;
        }
    }

    HeapFree(GetProcessHeap(), 0, valCaps);

    if (!foundCC || numSlots == 0)
    {
        if (!s_ptpInitDiagLogged)
            SZ_LOG_WARN("PTP", L"initPtpDevice: No Contact Count (foundCC=%d) or Contact ID (numSlots=%d) found in %u value caps — device is not a PTP or uses unexpected descriptor",
                        foundCC ? 1 : 0, numSlots, numValCaps);
        s_ptpInitDiagLogged = true;
        HeapFree(GetProcessHeap(), 0, ppd);
        return false;
    }

    // Store device info (clean up previous if switching devices)
    if (s_ptpDevice.preparsedData)
        HeapFree(GetProcessHeap(), 0, s_ptpDevice.preparsedData);

    s_ptpDevice.preparsedData = ppd;
    s_ptpDevice.valid = true;
    s_ptpDevice.contactCountLC = contactCountLC;
    s_ptpDevice.numSlots = numSlots;
    for (int i = 0; i < numSlots; i++)
        s_ptpDevice.slots[i].linkCollection = slotLCs[i];
    s_ptpDeviceHandle = hDevice;

    // Reset tracking state and diagnostics flag (allow logging for future device changes)
    for (auto& c : s_ptpContacts) c = {};
    s_ptpScrollRemainder = 0;
    s_ptpInitDiagLogged = false;

    SZ_LOG_INFO("Main", L"PTP device initialized: %d contact slot(s), contactCountLC=%u",
                numSlots, contactCountLC);
    return true;
}

// ── PTP HID Report Processing ────────────────────────────────────────────────
// Extracts contact data from each report, detects two-finger vertical pan,
// and converts to scroll delta when modifier key is held.

static void handlePtpHidReport(const BYTE* reportData, DWORD reportSize)
{
    auto* ppd = s_ptpDevice.preparsedData;
    // HidP functions take non-const PCHAR — they don't modify the buffer
    auto* report = reinterpret_cast<PCHAR>(const_cast<BYTE*>(reportData));

    // Extract contact count (present in end-of-frame report, or every report)
    ULONG contactCount = 0;
    [[maybe_unused]] NTSTATUS ccStatus =
        HidP_GetUsageValue(HidP_Input, 0x0D, s_ptpDevice.contactCountLC, 0x54,
                           &contactCount, ppd, report, reportSize);
    SZ_LOG_DEBUG("PTP", L"handlePtpHidReport: contactCount=%lu (status=0x%08lX, reportSize=%lu)",
                 contactCount, static_cast<unsigned long>(ccStatus), reportSize);

    // Extract data from each contact slot in this report
    for (int slot = 0; slot < s_ptpDevice.numSlots; slot++)
    {
        USHORT lc = s_ptpDevice.slots[slot].linkCollection;

        // Contact ID
        ULONG contactId = 0;
        if (HidP_GetUsageValue(HidP_Input, 0x0D, lc, 0x51, &contactId, ppd,
                               report, reportSize) != HIDP_STATUS_SUCCESS)
            continue;

        // Tip Switch (button usage 0x42 on digitizer page)
        USAGE usages[8] = {};
        ULONG usageLen = 8;
        bool tipSwitch = false;
        if (HidP_GetUsages(HidP_Input, 0x0D, lc, usages, &usageLen, ppd,
                           report, reportSize) == HIDP_STATUS_SUCCESS)
        {
            for (ULONG u = 0; u < usageLen; u++)
            {
                if (usages[u] == 0x42) { tipSwitch = true; break; }
            }
        }

        // Y position (Generic Desktop page 0x01, same link collection as contact)
        ULONG y = 0;
        HidP_GetUsageValue(HidP_Input, 0x01, lc, 0x31, &y, ppd, report, reportSize);

        // Update per-contact tracking state (clamped to valid ID range)
        if (contactId < 5)
        {
            auto& cs = s_ptpContacts[contactId];
            if (tipSwitch)
            {
                cs.prevY = cs.y;
                cs.hasPrev = cs.active; // Valid prev only if was already active
                cs.y = static_cast<LONG>(y);
                cs.active = true;
            }
            else
            {
                cs.active = false;
                cs.hasPrev = false;
            }
        }
    }

    // Only process when we have a complete frame with 2+ contacts
    if (contactCount < 2)
    {
        SZ_LOG_DEBUG("PTP", L"handlePtpHidReport: skipping, contactCount=%lu < 2", contactCount);
        return;
    }

    // Find first two active contacts with valid Y deltas
    int scrollFingers = 0;
    LONG totalDeltaY = 0;

    for (int i = 0; i < 5 && scrollFingers < 2; i++)
    {
        auto& c = s_ptpContacts[i];
        if (c.active && c.hasPrev)
        {
            totalDeltaY += (c.y - c.prevY);
            scrollFingers++;
        }
    }

    if (scrollFingers < 2)
    {
        SZ_LOG_DEBUG("PTP", L"handlePtpHidReport: scrollFingers=%d < 2 (contactCount=%lu)",
                     scrollFingers, contactCount);
        return;
    }

    LONG avgDeltaY = totalDeltaY / scrollFingers;
    SZ_LOG_DEBUG("PTP", L"handlePtpHidReport: scrollFingers=%d, totalDeltaY=%ld, avgDeltaY=%ld",
                 scrollFingers, totalDeltaY, avgDeltaY);
    if (avgDeltaY == 0)
        return;

    // Dedup with LL hook: skip if hook handled scroll recently
    int64_t lastLL = g_sharedState.lastLLHookScrollTime.load(std::memory_order_relaxed);
    int64_t now = static_cast<int64_t>(GetTickCount64());
    if (now - lastLL < 50)
    {
        SZ_LOG_DEBUG("PTP", L"handlePtpHidReport: dedup suppressed (now-lastLL=%lld ms)",
                     now - lastLL);
        return;
    }

    // Check modifier key
    auto snap = std::atomic_load(&g_sharedState.settingsSnapshot);
    int modVK = snap ? snap->modifierKeyVK : VK_LWIN;
    int genericVK = SmoothZoom::toGenericVK(modVK);
    bool modHeld = (GetAsyncKeyState(genericVK) & 0x8000) != 0;
    SZ_LOG_DEBUG("PTP", L"handlePtpHidReport: modVK=0x%X, genericVK=0x%X, modHeld=%d, asyncState=0x%04X",
                 modVK, genericVK, modHeld ? 1 : 0,
                 static_cast<unsigned>(GetAsyncKeyState(genericVK) & 0xFFFF));
    if (!modHeld)
        return;

    // Convert PTP device-unit Y delta to WHEEL_DELTA (120) units.
    // PTP Y increases downward. Fingers moving down = positive avgDeltaY = scroll down.
    // WHEEL_DELTA positive = scroll up. Negate for traditional Windows scroll direction.
    //
    // Natural scrolling compensation: when the OS "natural scrolling" setting is ON,
    // the LL hook's WM_MOUSEWHEEL delta is pre-flipped by Windows — but raw HID data
    // is not. Without this check, the PTP path produces the opposite direction from
    // the LL hook path when natural scrolling is enabled.
    // Natural OFF: negate (traditional: fingers down → scroll up → positive WHEEL_DELTA)
    // Natural ON:  don't negate (OS already flips LL hook; match that convention)
    int32_t adjustedDeltaY = s_ptpNaturalScrolling ? avgDeltaY : -avgDeltaY;
    s_ptpScrollRemainder += adjustedDeltaY;

    int32_t notches = s_ptpScrollRemainder / kPtpUnitsPerNotch;
    SZ_LOG_DEBUG("PTP", L"handlePtpHidReport: remainder=%d, notches=%d (kPtpUnitsPerNotch=%d)",
                 s_ptpScrollRemainder, notches, kPtpUnitsPerNotch);
    if (notches != 0)
    {
        s_ptpScrollRemainder -= notches * kPtpUnitsPerNotch;
        int16_t delta = static_cast<int16_t>(notches * WHEEL_DELTA);

        SZ_LOG_DEBUG("Main", L"PTP scroll: avgDY=%d notches=%d delta=%d",
                     avgDeltaY, notches, delta);
        g_sharedState.scrollAccumulator.fetch_add(delta, std::memory_order_release);
        g_sharedState.modifierHeld.store(true, std::memory_order_relaxed);
    }
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
                SZ_LOG_WARN("Main", L"Hook deregistration detected, reinstalling...");
                bool restored = g_inputInterceptor.reinstall();
                if (restored && s_hookFailureNotified)
                {
                    // Recovery after previous failure — inform user
                    g_trayUI.showBalloonNotification(
                        L"SmoothZoom",
                        L"Input hooks restored successfully.");
                    s_hookFailureNotified = false;
                }
                else if (!restored && !s_hookFailureNotified && !s_sessionLocked)
                {
                    // First failure — notify once, suppress further spam.
                    // Suppress during session lock (AC-ERR.04): hooks are expected
                    // to fail on the secure desktop.
                    g_trayUI.showBalloonNotification(
                        L"SmoothZoom — Input Error",
                        L"Input hooks could not be reinstalled. "
                        L"Zoom gestures may not work until restart.");
                    s_hookFailureNotified = true;
                }
            }
        }
        else if (wParam == kTrayIconTimerId)
        {
            // Poll zoom level and update tray icon on state transitions
            float zoom = g_sharedState.currentZoomLevel.load(std::memory_order_relaxed);
            bool isZoomed = (zoom > 1.005f);
            if (isZoomed != s_trayIconShowsZoomed)
            {
                s_trayIconShowsZoomed = isZoomed;
                g_trayUI.updateTrayIcon(isZoomed);
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

    case WM_UPDATE_TRAY_ICON:
        g_trayUI.updateTrayIcon(wParam != 0);
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case IDM_SETTINGS:    g_trayUI.showSettingsWindow(); return 0;
        case IDM_TOGGLE_ZOOM: g_sharedState.commandQueue.push(SmoothZoom::ZoomCommand::TrayToggle); return 0;
        case IDM_EXIT:        g_trayUI.requestGracefulExit(); return 0;
        }
        break;

    case WM_DISPLAYCHANGE:
        // Screen resolution / monitor config changed — update virtual screen metrics (AC-ERR.05, E6.4–E6.7)
        g_sharedState.screenWidth.store(GetSystemMetrics(SM_CXVIRTUALSCREEN), std::memory_order_relaxed);
        g_sharedState.screenHeight.store(GetSystemMetrics(SM_CYVIRTUALSCREEN), std::memory_order_relaxed);
        g_sharedState.screenOriginX.store(GetSystemMetrics(SM_XVIRTUALSCREEN), std::memory_order_relaxed);
        g_sharedState.screenOriginY.store(GetSystemMetrics(SM_YVIRTUALSCREEN), std::memory_order_relaxed);
        return 0;

    case WM_WTSSESSION_CHANGE:
        // Session lock/unlock notifications (AC-ERR.04, E6.10)
        if (wParam == WTS_SESSION_LOCK)
        {
            s_sessionLocked = true;
            SZ_LOG_INFO("Main", L"Session locked \u2014 suppressing hook alerts");
        }
        else if (wParam == WTS_SESSION_UNLOCK)
        {
            s_sessionLocked = false;
            SZ_LOG_INFO("Main", L"Session unlocked \u2014 checking hook health");
            // Force immediate hook health check after returning from secure desktop
            if (!g_inputInterceptor.isHealthy())
            {
                bool restored = g_inputInterceptor.reinstall();
                if (restored)
                {
                    s_hookFailureNotified = false;
                    SZ_LOG_INFO("Main", L"Hooks restored after unlock");
                }
            }
        }
        return 0;

    case WM_SETTINGCHANGE:
        // Refresh touchpad natural scrolling setting when user changes it in Windows Settings.
        // WM_SETTINGCHANGE is broadcast system-wide on control panel / Settings changes.
        s_ptpNaturalScrolling = queryTouchpadNaturalScrolling();
        SZ_LOG_DEBUG("Main", L"WM_SETTINGCHANGE: touchpad natural scrolling now %s",
                     s_ptpNaturalScrolling ? L"ON" : L"OFF");
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
            removeSentinel(s_sentinelPath);
        }
        return 0;

    case WM_INPUT:
    {
        // Raw Input fallback for scroll events that bypass LL mouse hooks.
        // Some touchpad drivers (Precision Touchpad) deliver scroll via WM_POINTER
        // to pointer-aware foreground windows (Desktop, Edge), skipping LL hooks.
        SZ_LOG_DEBUG("PTP", L"WM_INPUT received (wParam=0x%X)", static_cast<unsigned>(wParam));

        UINT dwSize = 0;
        GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam), RID_INPUT,
                        nullptr, &dwSize, sizeof(RAWINPUTHEADER));
        if (dwSize == 0)
        {
            SZ_LOG_DEBUG("PTP", L"WM_INPUT: dwSize=0, skipping");
            return DefWindowProcW(hWnd, msg, wParam, lParam);
        }

        // Stack buffer — RAWINPUT is ~48 bytes for mouse, but HID can be larger
        alignas(8) BYTE buf[512];
        if (dwSize > sizeof(buf))
            return DefWindowProcW(hWnd, msg, wParam, lParam);
        if (GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam), RID_INPUT,
                            buf, &dwSize, sizeof(RAWINPUTHEADER)) == UINT(-1))
            return DefWindowProcW(hWnd, msg, wParam, lParam);

        auto* raw = reinterpret_cast<RAWINPUT*>(buf);

        SZ_LOG_DEBUG("PTP", L"WM_INPUT: dwType=%lu, dwSize=%u",
                     raw->header.dwType, dwSize);

        if (raw->header.dwType == RIM_TYPEHID)
        {
            // Precision Touchpad HID report — parse for two-finger scroll
            HANDLE hDevice = raw->header.hDevice;
            SZ_LOG_DEBUG("PTP", L"RIM_TYPEHID: hDevice=0x%p, count=%lu, sizeHid=%lu",
                         hDevice, raw->data.hid.dwCount, raw->data.hid.dwSizeHid);

            // One-time device init: parse report descriptor for contact slots
            if (hDevice != s_ptpDeviceHandle || !s_ptpDevice.valid)
            {
                if (!initPtpDevice(hDevice))
                    return DefWindowProcW(hWnd, msg, wParam, lParam);
            }

            // Process each HID report in this WM_INPUT message
            DWORD count = raw->data.hid.dwCount;
            DWORD size  = raw->data.hid.dwSizeHid;
            const BYTE* data = raw->data.hid.bRawData;
            for (DWORD r = 0; r < count; r++)
                handlePtpHidReport(data + r * size, size);

            return DefWindowProcW(hWnd, msg, wParam, lParam);
        }

        if (raw->header.dwType != RIM_TYPEMOUSE)
            return DefWindowProcW(hWnd, msg, wParam, lParam);

        const RAWMOUSE& rm = raw->data.mouse;
        int16_t delta = 0;
        if (rm.usButtonFlags & RI_MOUSE_WHEEL)
            delta = static_cast<int16_t>(rm.usButtonData);
        else if (rm.usButtonFlags & RI_MOUSE_HWHEEL)
            delta = static_cast<int16_t>(rm.usButtonData);

        if (delta == 0)
            return DefWindowProcW(hWnd, msg, wParam, lParam);

        // Dedup: skip if LL hook already handled a scroll within 50ms
        int64_t lastLL = g_sharedState.lastLLHookScrollTime.load(std::memory_order_relaxed);
        int64_t now = static_cast<int64_t>(GetTickCount64());
        if (now - lastLL < 50)
            return DefWindowProcW(hWnd, msg, wParam, lParam);

        // Check modifier via GetAsyncKeyState (can't access WinKeyManager from here)
        auto snap = std::atomic_load(&g_sharedState.settingsSnapshot);
        int modVK = snap ? snap->modifierKeyVK : VK_LWIN;
        int genericVK = SmoothZoom::toGenericVK(modVK);
        bool modHeld = (GetAsyncKeyState(genericVK) & 0x8000) != 0;

        if (modHeld)
        {
            SZ_LOG_INFO("Main", L"Raw Input scroll fallback: delta=%d", delta);
            g_sharedState.scrollAccumulator.fetch_add(delta, std::memory_order_release);
            g_sharedState.modifierHeld.store(true, std::memory_order_relaxed);
        }

        return DefWindowProcW(hWnd, msg, wParam, lParam);
    }

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
    wc.hIcon = LoadIconW(hInstance, MAKEINTRESOURCE(IDI_APP_ICON));
    wc.hIconSm = LoadIconW(hInstance, MAKEINTRESOURCE(IDI_APP_ICON));
    wc.lpszClassName = L"SmoothZoomMsgWindow";

    RegisterClassExW(&wc);

    // Hidden top-level window (not HWND_MESSAGE) so it receives broadcast
    // messages like WM_DISPLAYCHANGE and WM_ENDSESSION. (AC-ERR.05)
    return CreateWindowExW(
        0, L"SmoothZoomMsgWindow", nullptr,
        0, 0, 0, 0, 0,
        nullptr,
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

    // Enable debug-level logging for diagnostics
    SmoothZoom::setLogLevel(SmoothZoom::LogLevel::Debug);

    // ── 0a. Dirty-shutdown sentinel check (R-14, E6.11) ─────────────────────
    s_sentinelPath = getSentinelPath();
    if (sentinelExists(s_sentinelPath))
    {
        // Previous session crashed while zoomed — reset magnification
        SZ_LOG_WARN("Main", L"Stale sentinel detected, resetting magnification...");
        SmoothZoom::MagBridge recoveryBridge;
        if (recoveryBridge.initialize())
        {
            recoveryBridge.shutdown();
        }
        removeSentinel(s_sentinelPath);
    }

    // ── 0b. Write fresh sentinel for this session ───────────────────────────
    writeSentinel(s_sentinelPath);

    // ── 0c. Load settings (Phase 5B: AC-2.9.01, AC-2.9.02) ─────────────────
    // Register observers BEFORE loading so the initial load triggers them.
    g_settingsManager.addObserver(publishToSharedState, &g_sharedState);
    SmoothZoom::InputInterceptor::registerSettingsObserver(g_settingsManager);
    g_configPath = SmoothZoom::SettingsManager::getDefaultConfigPath();

    // ── 0c½. Initialize file logging alongside config.json ──────────────────
    if (!g_configPath.empty())
    {
        std::filesystem::path logPath = std::filesystem::path(g_configPath).parent_path() / L"smoothzoom.log";
        SmoothZoom::initLogFile(logPath.c_str());
    }

    if (!g_configPath.empty())
        g_settingsManager.loadFromFile(g_configPath.c_str());
    // Ensure SharedState has settings even if load failed (defaults apply):
    {
        auto snap = g_settingsManager.snapshot();
        std::atomic_store(&g_sharedState.settingsSnapshot, snap);
        g_sharedState.settingsVersion.store(
            g_settingsManager.version(), std::memory_order_release);
    }

    // Initialize virtual screen dimensions in shared state (read by render thread)
    g_sharedState.screenWidth.store(GetSystemMetrics(SM_CXVIRTUALSCREEN), std::memory_order_relaxed);
    g_sharedState.screenHeight.store(GetSystemMetrics(SM_CYVIRTUALSCREEN), std::memory_order_relaxed);
    g_sharedState.screenOriginX.store(GetSystemMetrics(SM_XVIRTUALSCREEN), std::memory_order_relaxed);
    g_sharedState.screenOriginY.store(GetSystemMetrics(SM_YVIRTUALSCREEN), std::memory_order_relaxed);

    // ── 0e. Conflict detection (AC-ERR.01, E6.8) ─────────────────────────────
    {
        DWORD magPid = findMagnifyExe();
        if (magPid != 0)
        {
            int choice = MessageBoxW(nullptr,
                L"Windows Magnifier is currently running.\n\n"
                L"SmoothZoom cannot operate while another full-screen magnifier "
                L"is active. Would you like to close Windows Magnifier and continue?",
                L"SmoothZoom \u2014 Conflict Detected",
                MB_YESNO | MB_ICONWARNING);

            if (choice == IDYES)
            {
                if (!terminateMagnifyExe(magPid))
                {
                    MessageBoxW(nullptr,
                        L"Failed to close Windows Magnifier.\n\n"
                        L"Please close it manually and try again.",
                        L"SmoothZoom \u2014 Error",
                        MB_OK | MB_ICONERROR);
                    removeSentinel(s_sentinelPath);
                    return 1;
                }
                Sleep(500); // Allow process cleanup
            }
            else
            {
                removeSentinel(s_sentinelPath);
                return 0;
            }
        }
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
        removeSentinel(s_sentinelPath);
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
        removeSentinel(s_sentinelPath);
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
        // Phase 6: Register for session lock/unlock notifications (AC-ERR.04, E6.10)
        WTSRegisterSessionNotification(g_msgWindow, NOTIFY_FOR_THIS_SESSION);

        // Query Windows touchpad scroll direction for PTP natural scrolling compensation
        s_ptpNaturalScrolling = queryTouchpadNaturalScrolling();
        SZ_LOG_INFO("Main", L"Touchpad natural scrolling: %s",
                    s_ptpNaturalScrolling ? L"ON" : L"OFF");

        // Raw Input fallback: catch touchpad scroll events that bypass LL hooks
        RAWINPUTDEVICE rids[2] = {};
        // 1. Generic mouse — catches external mouse wheel events
        rids[0].usUsagePage = 0x01;  // HID_USAGE_PAGE_GENERIC
        rids[0].usUsage     = 0x02;  // HID_USAGE_GENERIC_MOUSE
        rids[0].dwFlags     = RIDEV_INPUTSINK;
        rids[0].hwndTarget  = g_msgWindow;
        // 2. Precision Touchpad — HID digitizer page
        rids[1].usUsagePage = 0x0D;  // HID_USAGE_PAGE_DIGITIZER
        rids[1].usUsage     = 0x05;  // HID_USAGE_GENERIC_TOUCHPAD
        rids[1].dwFlags     = RIDEV_INPUTSINK;
        rids[1].hwndTarget  = g_msgWindow;
        if (!RegisterRawInputDevices(rids, 2, sizeof(RAWINPUTDEVICE)))
        {
            // Touchpad registration may fail if no PTP device — try mouse only
            SZ_LOG_WARN("Main", L"RegisterRawInputDevices (mouse+touchpad) failed, trying mouse only");
            if (!RegisterRawInputDevices(rids, 1, sizeof(RAWINPUTDEVICE)))
                SZ_LOG_WARN("Main", L"RegisterRawInputDevices (mouse only) also failed");
            else
                SZ_LOG_INFO("Main", L"RegisterRawInputDevices (mouse only) succeeded");
        }
        else
            SZ_LOG_INFO("Main", L"RegisterRawInputDevices (mouse+touchpad) succeeded");
    }

    // Phase 5C: Register TaskbarCreated for Explorer restart detection
    WM_TASKBAR_CREATED = RegisterWindowMessageW(L"TaskbarCreated");

    // ── 2d. Create tray icon (Phase 5C: AC-2.9.13–16) ──────────────────────
    g_trayUI.create(hInstance, g_msgWindow, g_sharedState, g_settingsManager,
                    g_configPath.c_str());

    // Start tray icon zoom-state polling timer
    if (g_msgWindow)
        SetTimer(g_msgWindow, kTrayIconTimerId, kTrayIconIntervalMs, nullptr);

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
        WTSUnRegisterSessionNotification(g_msgWindow);
        KillTimer(g_msgWindow, kWatchdogTimerId);
        KillTimer(g_msgWindow, kTrayIconTimerId);
        DestroyWindow(g_msgWindow);
        g_msgWindow = nullptr;
    }

    // Stop UIA monitors first (they write to shared state read by render thread)
    g_caretMonitor.stop();
    g_focusMonitor.stop();

    g_renderLoop.requestShutdown();

    // Wait for render thread to finish (it resets zoom to 1.0× on its thread).
    // Timeout after 3 seconds to prevent process hang if render thread is stuck.
    {
        DWORD shutdownStart = GetTickCount();
        while (g_renderLoop.isRunning())
        {
            if (GetTickCount() - shutdownStart > 3000)
            {
                SZ_LOG_ERROR("Main", L"Render thread shutdown timed out (3s)");
                break;
            }
            Sleep(10);
        }
    }

    // No-op — MagUninitialize now happens on the render thread (thread affinity).
    g_renderLoop.finalizeShutdown();

    g_inputInterceptor.uninstall();

    // ── 4b. Clean up PTP HID state ──────────────────────────────────────────
    if (s_ptpDevice.preparsedData)
    {
        HeapFree(GetProcessHeap(), 0, s_ptpDevice.preparsedData);
        s_ptpDevice.preparsedData = nullptr;
    }

    // ── 4c. Remove sentinel on clean exit (R-14) ───────────────────────────
    removeSentinel(s_sentinelPath);

    return 0;
}

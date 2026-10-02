// =============================================================================
// SmoothZoom â€” Dev tools: --record / --bench / --reset / --probe, watchdog.
// Compiled only with SMOOTHZOOM_DEV_TOOLS=ON (see DevTools.h, Doc 3 Â§7).
//
// Threading:
//   Main thread  â€” arg parsing, trace replay (runBench), record drain, output
//                  writing. Replay injects through InputInterceptor's own wheel
//                  decision, exactly as the WH_MOUSE_LL callback would.
//   Render threadâ€” only writes TransformSample PODs into the buffer allocated
//                  here (RenderLoop.cpp); never touches anything else in this file.
//   Watchdog     â€” ONE extra thread (dev builds only): waits on the quit event
//                  with the --timeout / --selfkill-ms deadline, then asks the
//                  main thread to exit and, if that hangs, resets magnification
//                  and terminates. Justification: a hung main thread cannot run
//                  its own timeout, and the existing shutdown watchdog is only
//                  armed after the message loop exits.
// =============================================================================

#include "DevTools.h"

#ifdef SMOOTHZOOM_DEV_TOOLS

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include "smoothzoom/common/SharedState.h"
#include "smoothzoom/input/InputInterceptor.h"
#include "smoothzoom/input/WheelDecision.h"
#include "smoothzoom/output/MagBridge.h"
#include "smoothzoom/support/Logger.h"

#include <windows.h>
#include <dwmapi.h>
#include <sddl.h>
#include <shellapi.h>

#include <json.hpp>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#pragma comment(lib, "Advapi32.lib")
#pragma comment(lib, "Shell32.lib")

namespace SmoothZoom
{
namespace DevTools
{

// â”€â”€ Options â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

static Mode         s_mode = Mode::Normal;
static std::wstring s_recordPath;
static std::wstring s_tracePath;
static std::wstring s_outDir;
static std::wstring s_probePath;
static int          s_timeoutSec = 0;      // 0 = no timeout
static int          s_selfKillMs = 0;
static bool         s_moveCursor = true;
static bool         s_cold = false;      // --cold: no warm-up (measure the first-gesture stall)
static int          s_settleMs = 750;
static std::wstring s_label;
static std::wstring s_configPath;   // --config
static bool         s_openSettings = false;   // --open-settings

static std::atomic<int>  s_exitCode{kExitOk};
static std::atomic<bool> s_abort{false};
static std::atomic<bool> s_quitRequested{false};

static const wchar_t* kQuitEventName = L"Local\\SmoothZoom.DevTools.Quit";

static int64_t qpcNow()
{
    LARGE_INTEGER q;
    QueryPerformanceCounter(&q);
    return q.QuadPart;
}

static int64_t qpcFreq()
{
    static int64_t f = [] { LARGE_INTEGER q; QueryPerformanceFrequency(&q); return q.QuadPart; }();
    return f;
}

static std::string narrow(const std::wstring& w)
{
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<size_t>(n > 0 ? n - 1 : 0), '\0');
    if (n > 1) WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, s.data(), n, nullptr, nullptr);
    return s;
}

bool parseCommandLine(std::wstring& err)
{
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) return true;
    bool ok = true;
    auto need = [&](int& i) -> const wchar_t* {
        if (i + 1 >= argc) { err = std::wstring(argv[i]) + L" needs a value"; ok = false; return L""; }
        return argv[++i];
    };
    for (int i = 1; i < argc && ok; ++i)
    {
        std::wstring a = argv[i];
        if (a == L"--record")           { s_mode = Mode::Record; s_recordPath = need(i); }
        else if (a == L"--bench")       { s_mode = Mode::Bench; s_tracePath = need(i); }
        else if (a == L"--out")         { s_outDir = need(i); }
        else if (a == L"--reset")       { s_mode = Mode::Reset; }
        else if (a == L"--probe")       { s_mode = Mode::Probe; s_probePath = need(i); }
        else if (a == L"--timeout")     { s_timeoutSec = _wtoi(need(i)); }
        else if (a == L"--selfkill-ms") { s_selfKillMs = _wtoi(need(i)); }
        else if (a == L"--settle-ms")   { s_settleMs = _wtoi(need(i)); }
        else if (a == L"--no-cursor")   { s_moveCursor = false; }
        else if (a == L"--cold")        { s_cold = true; }
        else if (a == L"--label")       { s_label = need(i); }
        else if (a == L"--config")      { s_configPath = need(i); }
        else if (a == L"--open-settings") { s_openSettings = true; }
        else { err = L"unknown argument: " + a; ok = false; }
    }
    if (ok && s_mode == Mode::Bench && s_outDir.empty()) { err = L"--bench needs --out <dir>"; ok = false; }
    if (ok && s_mode == Mode::Bench && s_timeoutSec == 0) s_timeoutSec = 180; // never run unbounded
    LocalFree(argv);
    return ok;
}

Mode mode() { return s_mode; }
bool unattended() { return s_mode == Mode::Bench; }
bool openSettingsAtStart() { return s_openSettings; }

std::string configOverridePath()
{
    if (s_configPath.empty())
        return {};
    std::error_code ec;
    std::filesystem::path p = std::filesystem::absolute(std::filesystem::path(s_configPath), ec);
    return (ec ? std::filesystem::path(s_configPath) : p).string();
}
void setExitCode(int code) { s_exitCode.store(code); }
int exitCode() { return s_exitCode.load(); }

// â”€â”€ Watchdog + quit event â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

static HANDLE s_quitEvent = nullptr;
static DWORD  s_mainThreadId = 0;
static int  (*s_emergencyReset)() = nullptr;

static HANDLE createQuitEvent()
{
    // DACL: SYSTEM + this user; mandatory label Medium/no-write-up so a medium-IL
    // script (deploy.ps1) can signal this UIAccess (higher-IL) process.
    HANDLE tok = nullptr;
    std::wstring sddl = L"D:P(A;;GA;;;SY)";
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok))
    {
        BYTE buf[256];
        DWORD len = 0;
        if (GetTokenInformation(tok, TokenUser, buf, sizeof(buf), &len))
        {
            LPWSTR sid = nullptr;
            if (ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(buf)->User.Sid, &sid))
            {
                sddl += L"(A;;GA;;;" + std::wstring(sid) + L")";
                LocalFree(sid);
            }
        }
        CloseHandle(tok);
    }
    sddl += L"S:(ML;;NW;;;ME)";
    SECURITY_ATTRIBUTES sa = { sizeof(sa), nullptr, FALSE };
    PSECURITY_DESCRIPTOR sd = nullptr;
    if (ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1, &sd, nullptr))
        sa.lpSecurityDescriptor = sd;
    HANDLE ev = CreateEventW(sa.lpSecurityDescriptor ? &sa : nullptr, TRUE, FALSE, kQuitEventName);
    if (!ev)
        SZ_LOG_WARN("DevTools", L"CreateEvent(%s) failed (GetLastError=%lu)", kQuitEventName, GetLastError());
    if (sd) LocalFree(sd);
    return ev;
}

static DWORD WINAPI watchdogThread(LPVOID)
{
    const int64_t start = GetTickCount64();
    const int64_t killAt = s_selfKillMs > 0 ? start + s_selfKillMs : INT64_MAX;
    const int64_t timeoutAt = s_timeoutSec > 0 ? start + int64_t(s_timeoutSec) * 1000 : INT64_MAX;

    for (;;)
    {
        int64_t now = GetTickCount64();
        int64_t next = std::min(killAt, timeoutAt);
        DWORD wait = (next == INT64_MAX) ? INFINITE : DWORD(std::max<int64_t>(0, next - now));
        DWORD r = s_quitEvent ? WaitForSingleObject(s_quitEvent, wait) : (Sleep(wait), WAIT_TIMEOUT);
        now = GetTickCount64();
        if (r == WAIT_OBJECT_0)
        {
            SZ_LOG_INFO("DevTools", L"Quit event signalled â€” requesting clean exit");
            s_quitRequested.store(true);
            if (s_mode == Mode::Bench) setExitCode(kExitWatchdog);
            break;
        }
        if (now >= killAt)
        {
            // Emulates an external kill: no handlers, no cleanup (R-14 test).
            SZ_LOG_WARN("DevTools", L"--selfkill-ms reached â€” TerminateProcess without cleanup");
            TerminateProcess(GetCurrentProcess(), kExitSelfKill);
        }
        if (now >= timeoutAt)
        {
            SZ_LOG_WARN("DevTools", L"--timeout %d s reached â€” requesting clean exit", s_timeoutSec);
            // --record uses --timeout as its normal end; only an unfinished bench fails.
            if (s_mode == Mode::Bench) setExitCode(kExitWatchdog);
            break;
        }
    }

    s_abort.store(true);
    PostThreadMessageW(s_mainThreadId, WM_QUIT, 0, 0);

    // Grace period for the normal shutdown (render thread resets to 1.0x,
    // MagUninitialize, unhook). If it hangs, reset and terminate.
    Sleep(10000);
    SZ_LOG_ERROR("DevTools", L"Clean exit did not complete in 10 s â€” emergency reset + TerminateProcess");
    if (s_emergencyReset) s_emergencyReset();
    TerminateProcess(GetCurrentProcess(), kExitForced);
    return 0;
}

void startWatchdog(unsigned long mainThreadId, int (*emergencyReset)())
{
    s_mainThreadId = mainThreadId;
    s_emergencyReset = emergencyReset;
    s_quitEvent = createQuitEvent();
    HANDLE h = CreateThread(nullptr, 0, watchdogThread, nullptr, 0, nullptr);
    if (h) CloseHandle(h);
}

// â”€â”€ Environment facts recorded with every output â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

static bool processHasUIAccess()
{
    HANDLE tok = nullptr;
    DWORD ui = 0, len = 0;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok))
    {
        GetTokenInformation(tok, TokenUIAccess, &ui, sizeof(ui), &len);
        CloseHandle(tok);
    }
    return ui != 0;
}

static const char* powerSource()
{
    SYSTEM_POWER_STATUS ps = {};
    if (!GetSystemPowerStatus(&ps)) return "unknown";
    return ps.ACLineStatus == 1 ? "AC" : ps.ACLineStatus == 0 ? "battery" : "unknown";
}

static std::string computerName()
{
    wchar_t buf[MAX_COMPUTERNAME_LENGTH + 1] = {};
    DWORD n = MAX_COMPUTERNAME_LENGTH + 1;
    GetComputerNameW(buf, &n);
    return narrow(buf);
}

static int64_t dwmRefreshPeriodQpc()
{
    DWM_TIMING_INFO ti = {};
    ti.cbSize = sizeof(ti);
    if (SUCCEEDED(DwmGetCompositionTimingInfo(nullptr, &ti)))
        return static_cast<int64_t>(ti.qpcRefreshPeriod);
    return 0;
}

// â”€â”€ --reset / --probe â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

int runResetMode()
{
    // Same sequence as the stale-sentinel recovery (main.cpp): initialize
    // (MagShowSystemCursor(TRUE)) then shutdown (transform 1.0, input transform
    // off, identity color effect, MagUninitialize).
    MagBridge bridge;
    if (!bridge.initialize())
        return kExitMagInitFailed;
    bridge.shutdown();
    return kExitOk;
}

int runProbeMode()
{
    // Read-only: report the current fullscreen transform without resetting it.
    MagBridge bridge;
    float mag = 0, x = 0, y = 0;
    bool init = bridge.initialize();
    bool got = init && bridge.getTransform(mag, x, y);
    std::ofstream f{std::filesystem::path(s_probePath)};
    f << "{\"initialized\":" << (init ? "true" : "false")
      << ",\"read\":" << (got ? "true" : "false")
      << ",\"magnification\":" << mag << ",\"x\":" << x << ",\"y\":" << y
      << ",\"uiaccess\":" << (processHasUIAccess() ? "true" : "false") << "}\n";
    // Deliberately no shutdown(): it would reset the transform being probed.
    return got ? kExitOk : kExitMagInitFailed;
}

// â”€â”€ Trace format â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
// CSV, '#' lines are "key: value" metadata. Columns (by header name):
//   t_us,qpc,source,delta,flags,injected,shift,ctrl,alt,win,x,y,mod_held,consumed
// source: WHEEL | HWHEEL | RAW | PTP | MARK | CONTACT | STROKE
//   MARK delta 1 = round-trip begin, 2 = end.
//   CONTACT delta = precision-touchpad fingers down after a change (0 = lifted).
//   STROKE = the fingers started a new stroke after a touch (delta unused).
//   PTP / RAW rows: every event those paths decided (mod_held = the modifier
//   at that event, consumed = zoomed). Older traces hold only the rows that
//   reached the accumulator, all with the modifier held: they replay the same.
// Metadata "touchpad_natural" (0/1, default 0) is the Windows touchpad
// scrolling direction the trace was recorded under; replay uses it, not the
// live setting.

enum class TraceKind : uint8_t { Wheel, HWheel, Raw, Ptp, Mark, Contact, Stroke };

static const char* kindName(TraceKind k)
{
    switch (k)
    {
    case TraceKind::Wheel: return "WHEEL";
    case TraceKind::HWheel: return "HWHEEL";
    case TraceKind::Raw: return "RAW";
    case TraceKind::Ptp: return "PTP";
    case TraceKind::Contact: return "CONTACT";
    case TraceKind::Stroke: return "STROKE";
    default: return "MARK";
    }
}

static const char* sourceName(WheelSource s)
{
    switch (s)
    {
    case WheelSource::HookWheel: return "WHEEL";
    case WheelSource::HookHWheel: return "HWHEEL";
    case WheelSource::RawInput: return "RAW";
    case WheelSource::Contact: return "CONTACT";
    case WheelSource::Stroke: return "STROKE";
    default: return "PTP";
    }
}

static const char* kTraceColumns = "t_us,qpc,source,delta,flags,injected,shift,ctrl,alt,win,x,y,mod_held,consumed";

struct TraceEvent
{
    int64_t   tUs = 0;
    TraceKind kind = TraceKind::Wheel;
    int16_t   delta = 0;
    uint8_t   mods = 0;
    int32_t   x = 0, y = 0;
    bool      hasPos = false;
};

struct InjectRecord
{
    int64_t qpc = 0;
    size_t  traceIndex = 0;
    bool    consumed = false;
    bool    modHeld = false;
    bool    decided = false;                    // went through the gesture tracker
    ScrollAction action = ScrollAction::Pass;
    ScrollDevice device = ScrollDevice::Mouse;
};

static std::vector<TraceEvent>             s_trace;
static std::map<std::string, std::string>  s_traceMeta;
static std::vector<InjectRecord>           s_injects;
static TransformTelemetry                  s_telemetry;
static std::vector<TransformSample>        s_sampleStore;
static bool                                s_benchCompleted = false;
static int64_t                             s_benchStartQpc = 0;
static int64_t                             s_benchEndQpc = 0;
static int64_t                             s_warmupEndQpc = 0;

static std::string trim(std::string s)
{
    while (!s.empty() && (s.back() == '\r' || s.back() == ' ' || s.back() == '\t')) s.pop_back();
    size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
    return s.substr(i);
}

static std::vector<std::string> splitCsv(const std::string& line)
{
    std::vector<std::string> out;
    std::stringstream ss(line);
    std::string cell;
    while (std::getline(ss, cell, ',')) out.push_back(trim(cell));
    return out;
}

static bool loadTrace(const std::wstring& path, std::wstring& err)
{
    std::ifstream f{std::filesystem::path(path)};
    if (!f) { err = L"cannot open trace " + path; return false; }
    std::string line;
    std::map<std::string, int> col;
    while (std::getline(f, line))
    {
        line = trim(line);
        if (line.size() >= 3 && (unsigned char)line[0] == 0xEF) line = line.substr(3); // BOM
        if (line.empty()) continue;
        if (line[0] == '#')
        {
            auto c = line.find(':');
            if (c != std::string::npos)
                s_traceMeta[trim(line.substr(1, c - 1))] = trim(line.substr(c + 1));
            continue;
        }
        auto cells = splitCsv(line);
        if (col.empty())
        {
            for (int i = 0; i < (int)cells.size(); ++i) col[cells[i]] = i;
            if (!col.count("t_us") || !col.count("source") || !col.count("delta"))
            { err = L"trace header must contain t_us, source, delta"; return false; }
            continue;
        }
        auto get = [&](const char* name) -> std::string {
            auto it = col.find(name);
            return (it != col.end() && it->second < (int)cells.size()) ? cells[it->second] : std::string();
        };
        TraceEvent e;
        e.tUs = std::stoll(get("t_us"));
        std::string src = get("source");
        if (src == "WHEEL") e.kind = TraceKind::Wheel;
        else if (src == "HWHEEL") e.kind = TraceKind::HWheel;
        else if (src == "RAW") e.kind = TraceKind::Raw;
        else if (src == "PTP") e.kind = TraceKind::Ptp;
        else if (src == "MARK") e.kind = TraceKind::Mark;
        else if (src == "CONTACT") e.kind = TraceKind::Contact;
        else if (src == "STROKE") e.kind = TraceKind::Stroke;
        else { err = L"unknown source in trace"; return false; }
        e.delta = static_cast<int16_t>(std::stoi(get("delta")));
        auto flag = [&](const char* n) { std::string v = get(n); return !v.empty() && v != "0"; };
        e.mods = static_cast<uint8_t>((flag("shift") ? kModShift : 0) | (flag("ctrl") ? kModCtrl : 0)
                                      | (flag("alt") ? kModAlt : 0) | (flag("win") ? kModWin : 0));
        std::string xs = get("x"), ys = get("y");
        if (!xs.empty() && !ys.empty()) { e.x = std::stoi(xs); e.y = std::stoi(ys); e.hasPos = true; }
        s_trace.push_back(e);
    }
    if (s_trace.empty()) { err = L"trace has no events"; return false; }
    std::stable_sort(s_trace.begin(), s_trace.end(),
                     [](const TraceEvent& a, const TraceEvent& b) { return a.tUs < b.tUs; });
    return true;
}

bool prepareBench(int& traceModifierVK)
{
    std::wstring err;
    try
    {
        if (!loadTrace(s_tracePath, err))
        {
            SZ_LOG_ERROR("DevTools", L"Bench: %s", err.c_str());
            return false;
        }
    }
    catch (...)
    {
        SZ_LOG_ERROR("DevTools", L"Bench: malformed trace %s", s_tracePath.c_str());
        return false;
    }
    traceModifierVK = 0;
    auto it = s_traceMeta.find("modifier_vk");
    if (it != s_traceMeta.end())
        traceModifierVK = static_cast<int>(std::stoul(it->second, nullptr, 0));

    // Telemetry: one sample per non-idle frame. Size for the trace duration plus
    // pre-roll/settle at up to 240 Hz; allocated here, before the render thread.
    double durS = double(s_trace.back().tUs) / 1e6 + 5.0 + s_settleMs / 1000.0;
    uint32_t cap = static_cast<uint32_t>(std::max(16384.0, durS * 240.0));
    s_sampleStore.assign(cap, TransformSample{});
    s_telemetry.samples = s_sampleStore.data();
    s_telemetry.capacity = cap;
    s_injects.reserve(s_trace.size());
    std::filesystem::create_directories(std::filesystem::path(s_outDir));
    return true;
}

TransformTelemetry* benchTelemetry() { return s_mode == Mode::Bench ? &s_telemetry : nullptr; }

// â”€â”€ Bench replay (main thread) â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

static HANDLE s_waitTimer = nullptr;

// Wait until QPC `target` while servicing the message queue (LL hooks and the
// app's windows keep working). Coarse wait on a high-resolution waitable timer,
// then a short spin for the last ~1 ms. Returns false if WM_QUIT/abort.
// Fingers down in the replayed trace. A trace logs CONTACT rows only when
// the count changes, but a live touchpad reports at >= 125 Hz while touched,
// and the gesture tracker treats contacts not refreshed for 250 ms as lifted.
// So the replay re-reports the current count every ~8 ms while it is > 0.
static int     s_replayContacts = 0;
static int64_t s_replayContactsQpc = 0;

static void refreshReplayContacts()
{
    if (s_replayContacts <= 0) return;
    const int64_t now = qpcNow();
    if (now - s_replayContactsQpc < qpcFreq() / 125) return;
    s_replayContactsQpc = now;
    InputInterceptor::onTouchpadContacts(s_replayContacts);
}

static bool waitUntil(int64_t target, bool (*pump)())
{
    const int64_t f = qpcFreq();
    for (;;)
    {
        if (!pump() || s_abort.load()) return false;
        refreshReplayContacts();
        int64_t remaining = target - qpcNow();
        if (remaining <= 0) return true;
        if (remaining > f / 666) // > 1.5 ms: sleep until ~1 ms before target
        {
            LARGE_INTEGER due;
            due.QuadPart = -std::max<int64_t>(1, (remaining - f / 1000) * 10000000 / f);
            if (s_waitTimer && SetWaitableTimer(s_waitTimer, &due, 0, nullptr, nullptr, FALSE))
                MsgWaitForMultipleObjectsEx(1, &s_waitTimer, INFINITE, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
            else
                MsgWaitForMultipleObjectsEx(0, nullptr, 1, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        }
        else
        {
            YieldProcessor();
        }
    }
}

void runBench(SharedState& state, bool (*pump)())
{
    const int64_t f = qpcFreq();
    s_waitTimer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    if (!s_waitTimer)
        s_waitTimer = CreateWaitableTimerExW(nullptr, nullptr, 0, TIMER_ALL_ACCESS);

    POINT savedCursor = {};
    GetCursorPos(&savedCursor);
    const int modVK = InputInterceptor::configuredModifierVK();

    // Touchpad direction the trace was recorded under (absent = 0, which
    // replays older traces exactly as before).
    {
        auto it = s_traceMeta.find("touchpad_natural");
        InputInterceptor::setTouchpadNaturalScrolling(it != s_traceMeta.end() && it->second == "1");
    }

    POINT lastPos = savedCursor;
    for (const auto& e : s_trace)
        if (e.hasPos) { lastPos = {e.x, e.y}; break; }
    if (s_moveCursor) SetCursorPos(lastPos.x, lastPos.y);

    // Pre-roll: let the render loop settle at 1.0x before telemetry starts.
    if (!waitUntil(qpcNow() + f * 6 / 10, pump)) goto done;

    s_telemetry.enabled.store(true, std::memory_order_relaxed);

    // Warm-up (default): the first transform away from 1.0x after MagInitialize
    // costs ~170-210 ms on TEMPLAPTOP (once per process). Take it here, one notch
    // in and then the Win+Esc reset path back to exactly 1.0x, so the trace
    // measures steady state. The stall is still reported as summary "cold_start".
    // --cold skips this to measure the first gesture itself.
    if (!s_cold)
    {
        InputInterceptor::devInjectWheel(WheelKind::Vertical, 120, true);
        if (!waitUntil(qpcNow() + f * 4 / 10, pump)) goto done;
        state.commandQueue.push(ZoomCommand::ResetZoom);
        if (!waitUntil(qpcNow() + f * 8 / 10, pump)) goto done;
    }
    s_warmupEndQpc = qpcNow();
    s_benchStartQpc = s_warmupEndQpc + f / 5; // 200 ms of baseline frames before event 0
    SZ_LOG_INFO("DevTools", L"Bench: replaying %zu events from %s", s_trace.size(), s_tracePath.c_str());

    for (size_t i = 0; i < s_trace.size(); ++i)
    {
        const TraceEvent& e = s_trace[i];
        if (!waitUntil(s_benchStartQpc + e.tUs * f / 1000000, pump)) goto done;

        if (s_moveCursor && e.hasPos && (e.x != lastPos.x || e.y != lastPos.y))
        {
            SetCursorPos(e.x, e.y);
            lastPos = {e.x, e.y};
        }

        InjectRecord r;
        r.traceIndex = i;
        r.modHeld = modifierHeldFromState(modVK, (e.mods & kModShift) != 0, (e.mods & kModCtrl) != 0,
                                          (e.mods & kModAlt) != 0, (e.mods & kModWin) != 0);
        r.qpc = qpcNow();
        ScrollVerdict v;
        switch (e.kind)
        {
        case TraceKind::Wheel:
        case TraceKind::HWheel:
            // The hook consumes everything but Pass (Swallow included).
            v = InputInterceptor::devInjectWheel(
                e.kind == TraceKind::Wheel ? WheelKind::Vertical : WheelKind::Horizontal, e.delta, r.modHeld);
            r.consumed = (v.action != ScrollAction::Pass);
            r.decided = true;
            break;
        case TraceKind::Raw:
        case TraceKind::Ptp:
            // The main.cpp paths' own decision (they observe, never consume:
            // consumed = zoomed).
            v = (e.kind == TraceKind::Ptp) ? InputInterceptor::onTouchpadScroll(e.delta, r.modHeld)
                                           : InputInterceptor::onRawWheel(e.delta, r.modHeld);
            r.consumed = (v.action == ScrollAction::Zoom);
            r.decided = true;
            break;
        case TraceKind::Contact:
            InputInterceptor::onTouchpadContacts(e.delta);
            s_replayContacts = e.delta;
            s_replayContactsQpc = qpcNow();
            break;
        case TraceKind::Stroke:
            InputInterceptor::onTouchpadStroke();
            break;
        case TraceKind::Mark:
            break;
        }
        r.action = v.action;
        r.device = v.device;
        s_injects.push_back(r);
    }

    if (!s_injects.empty() && !waitUntil(s_injects.back().qpc + int64_t(s_settleMs) * f / 1000, pump))
        goto done;
    s_benchCompleted = true;

done:
    s_benchEndQpc = qpcNow();
    s_telemetry.enabled.store(false, std::memory_order_relaxed);
    SetCursorPos(savedCursor.x, savedCursor.y);
    if (s_waitTimer) { CloseHandle(s_waitTimer); s_waitTimer = nullptr; }
    SZ_LOG_INFO("DevTools", L"Bench: replay %s (%zu/%zu events)",
                s_benchCompleted ? L"complete" : L"ABORTED", s_injects.size(), s_trace.size());
}

// â”€â”€ --record â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
// Ring written by the hook tap and the RAW/PTP taps, drained by a main-thread
// timer. All on the main thread: no atomics needed. Static storage, so no
// allocation ever happens inside the hook.

static constexpr uint32_t kRingSize = 1u << 16;
static WheelTapEvent s_ring[kRingSize];
static uint32_t s_ringHead = 0, s_ringTail = 0, s_ringDropped = 0;
static FILE*    s_recordFile = nullptr;
static int64_t  s_recordStartQpc = 0;
static uint64_t s_recordCount = 0;

static void ringPush(const WheelTapEvent& e)
{
    if (s_ringHead - s_ringTail >= kRingSize) { ++s_ringDropped; return; }
    s_ring[s_ringHead++ & (kRingSize - 1)] = e;
}

static void recordDrain()
{
    if (!s_recordFile) return;
    const int64_t f = qpcFreq();
    while (s_ringTail != s_ringHead)
    {
        const WheelTapEvent& e = s_ring[s_ringTail++ & (kRingSize - 1)];
        long long tUs = (long long)((e.qpc - s_recordStartQpc) * 1000000 / f);
        fprintf(s_recordFile, "%lld,%lld,%s,%d,%u,%d,%d,%d,%d,%d,%d,%d,%d,%d\n",
                tUs, (long long)e.qpc, sourceName(e.source), (int)e.delta, (unsigned)e.flags,
                (e.flags & 1) ? 1 : 0,  // LLMHF_INJECTED
                (e.mods & kModShift) ? 1 : 0, (e.mods & kModCtrl) ? 1 : 0,
                (e.mods & kModAlt) ? 1 : 0, (e.mods & kModWin) ? 1 : 0,
                e.x, e.y, e.modifierHeld ? 1 : 0, e.consumed ? 1 : 0);
        ++s_recordCount;
    }
    fflush(s_recordFile);
}

static void CALLBACK recordTimerProc(HWND, UINT, UINT_PTR, DWORD) { recordDrain(); }

static void hookTap(const WheelTapEvent& e) { ringPush(e); }

bool startRecord(SharedState& state)
{
    // Create the trace's folder (like --bench does for --out): a missing folder
    // used to fail the open, and the run then idled to --timeout recording nothing.
    {
        std::error_code ec;
        std::filesystem::path parent = std::filesystem::path(s_recordPath).parent_path();
        if (!parent.empty())
            std::filesystem::create_directories(parent, ec);
    }
    if (_wfopen_s(&s_recordFile, s_recordPath.c_str(), L"w") != 0)
        s_recordFile = nullptr;
    if (!s_recordFile)
    {
        SZ_LOG_ERROR("DevTools", L"Record: cannot open %s", s_recordPath.c_str());
        return false;
    }
    s_recordStartQpc = qpcNow();
    auto snap = std::atomic_load(&state.settingsSnapshot);
    int64_t period = dwmRefreshPeriodQpc();
    fprintf(s_recordFile, "# smoothzoom-trace: 1\n# kind: record\n# computer: %s\n# qpc_freq: %lld\n"
            "# qpc_start: %lld\n# screen: %dx%d origin %d,%d\n# refresh_period_ms: %.4f\n# power: %s\n"
            "# modifier_vk: 0x%X\n# reverse_scroll: %d\n# scroll_sensitivity: %.3f\n"
            "# touchpad_natural: %d\n# touchpad_reverse: %d\n# touchpad_sensitivity: %.3f\n# touchpad_coast: %d\n",
            computerName().c_str(), (long long)qpcFreq(), (long long)s_recordStartQpc,
            state.screenWidth.load(), state.screenHeight.load(), state.screenOriginX.load(), state.screenOriginY.load(),
            period > 0 ? double(period) * 1000.0 / double(qpcFreq()) : 0.0, powerSource(),
            InputInterceptor::configuredModifierVK(),
            snap ? (snap->reverseScrollDirection ? 1 : 0) : 0, snap ? snap->scrollSensitivity : 1.0f,
            InputInterceptor::touchpadNaturalScrolling() ? 1 : 0,
            snap ? (snap->touchpadReverseScrollDirection ? 1 : 0) : 0,
            snap ? snap->touchpadSensitivity : 1.0f, snap ? (snap->momentumZoom ? 1 : 0) : 1);
    fprintf(s_recordFile, "%s\n", kTraceColumns);
    fflush(s_recordFile);
    InputInterceptor::setWheelTap(hookTap);
    SetTimer(nullptr, 0, 200, recordTimerProc);
    SZ_LOG_INFO("DevTools", L"Record: logging wheel events to %s", s_recordPath.c_str());
    return true;
}

// â”€â”€ Bench metrics â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

using json = nlohmann::ordered_json;

static double pct(std::vector<double> v, double p)
{
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    double r = p / 100.0 * double(v.size() - 1);
    size_t lo = size_t(std::floor(r)), hi = size_t(std::ceil(r));
    return v[lo] + (v[hi] - v[lo]) * (r - double(lo));
}

static json stats(const std::vector<double>& v)
{
    json j;
    j["n"] = v.size();
    if (v.empty()) { j["p50"] = nullptr; j["p95"] = nullptr; j["p99"] = nullptr; j["max"] = nullptr; return j; }
    j["p50"] = pct(v, 50);
    j["p95"] = pct(v, 95);
    j["p99"] = pct(v, 99);
    j["max"] = *std::max_element(v.begin(), v.end());
    return j;
}

static int64_t floorDiv(int64_t a, int64_t b) { int64_t q = a / b; if ((a % b != 0) && ((a < 0) != (b < 0))) --q; return q; }

struct Upd { int64_t t; int64_t tEnd; int64_t refresh; size_t s; };

static json computeSummary(const TransformSample* smp, uint32_t n, uint32_t dropped)
{
    const double f = double(qpcFreq());
    auto ms = [&](int64_t q) { return double(q) * 1000.0 / f; };

    std::vector<int64_t> periods;
    for (uint32_t i = 0; i < n; ++i) if (smp[i].qpcRefreshPeriod > 0) periods.push_back(smp[i].qpcRefreshPeriod);
    if (periods.empty()) { int64_t p = dwmRefreshPeriodQpc(); if (p > 0) periods.push_back(p); }
    std::sort(periods.begin(), periods.end());
    const int64_t P = periods.empty() ? 0 : periods[periods.size() / 2];
    const double periodMs = P > 0 ? ms(P) : 0.0;

    auto refreshOf = [&](const TransformSample& s, int64_t t) -> int64_t {
        if (P <= 0 || s.qpcVBlank == 0) return floorDiv(t - s_benchStartQpc, P > 0 ? P : 1);
        return int64_t(s.cRefresh) + floorDiv(t - s.qpcVBlank, P);
    };

    std::vector<Upd> upd;
    size_t failed = 0;
    for (uint32_t i = 0; i < n; ++i)
        if (smp[i].updated)
        {
            upd.push_back({smp[i].qpcSetBegin, smp[i].qpcSetEnd, refreshOf(smp[i], smp[i].qpcSetBegin), i});
            if (!smp[i].ok) ++failed;
        }

    // Consumed input events, grouped into bursts (gap <= 100 ms = one gesture).
    constexpr double kBurstGapMs = 100.0;
    struct In { int64_t t; };
    std::vector<In> inputs;
    std::vector<std::pair<int64_t, int>> marks;
    for (const auto& r : s_injects)
    {
        const TraceEvent& e = s_trace[r.traceIndex];
        if (e.kind == TraceKind::Mark) { marks.push_back({r.qpc, e.delta}); continue; }
        if (r.consumed) inputs.push_back({r.qpc});
    }
    struct Burst { size_t first, last; };
    std::vector<Burst> bursts;
    for (size_t i = 0; i < inputs.size(); ++i)
    {
        if (bursts.empty() || ms(inputs[i].t - inputs[bursts.back().last].t) > kBurstGapMs)
            bursts.push_back({i, i});
        else
            bursts.back().last = i;
    }

    auto firstUpdAtOrAfter = [&](int64_t t) {
        return size_t(std::lower_bound(upd.begin(), upd.end(), t,
                                       [](const Upd& u, int64_t v) { return u.t < v; }) - upd.begin());
    };

    std::vector<double> intervalMs, intervalPeriods, settleMs, latUpdMs, latVblMs, stepLog;
    uint64_t activeRefreshes = 0, missed = 0, noUpdateInputs = 0;
    for (size_t b = 0; b < bursts.size(); ++b)
    {
        int64_t bStart = inputs[bursts[b].first].t;
        int64_t bLast = inputs[bursts[b].last].t;
        int64_t bEnd = (b + 1 < bursts.size()) ? inputs[bursts[b + 1].first].t : INT64_MAX;
        size_t u0 = firstUpdAtOrAfter(bStart), u1 = u0;
        while (u1 < upd.size() && upd[u1].t < bEnd) ++u1;
        if (u1 == u0) continue;
        // Refresh coverage while this gesture is in motion.
        int64_t r0 = upd[u0].refresh, r1 = upd[u1 - 1].refresh;
        std::vector<int64_t> rs;
        for (size_t k = u0; k < u1; ++k) rs.push_back(upd[k].refresh);
        std::sort(rs.begin(), rs.end());
        rs.erase(std::unique(rs.begin(), rs.end()), rs.end());
        activeRefreshes += uint64_t(r1 - r0 + 1);
        missed += uint64_t(r1 - r0 + 1) - rs.size();
        for (size_t k = u0 + 1; k < u1; ++k)
        {
            double d = ms(upd[k].t - upd[k - 1].t);
            intervalMs.push_back(d);
            if (periodMs > 0) intervalPeriods.push_back(d / periodMs);
            float z0 = smp[upd[k - 1].s].zoom, z1 = smp[upd[k].s].zoom;
            if (z0 > 0 && z1 > 0) stepLog.push_back(std::fabs(std::log(double(z1) / double(z0))));
        }
        settleMs.push_back(std::max(0.0, ms(upd[u1 - 1].t - bLast)));
        // Per-input latency to the first transform update that includes it.
        for (size_t i = bursts[b].first; i <= bursts[b].last; ++i)
        {
            size_t k = firstUpdAtOrAfter(inputs[i].t);
            if (k >= u1) { ++noUpdateInputs; continue; }
            latUpdMs.push_back(ms(upd[k].t - inputs[i].t));
            const TransformSample& s = smp[upd[k].s];
            if (P > 0 && s.qpcVBlank)
            {
                int64_t vNext = s.qpcVBlank + (floorDiv(upd[k].tEnd - s.qpcVBlank, P) + 1) * P;
                latVblMs.push_back(ms(vNext - inputs[i].t));
            }
        }
    }

    // Refreshes that received more than one transform update (anywhere in the run).
    std::map<int64_t, int> perRefresh;
    for (const auto& u : upd) perRefresh[u.refresh]++;
    uint64_t multi = 0;
    for (const auto& kv : perRefresh) if (kv.second > 1) ++multi;

    // Phase of each update within its refresh interval.
    std::vector<double> phaseMs;
    for (const auto& u : upd)
    {
        const TransformSample& s = smp[u.s];
        if (P > 0 && s.qpcVBlank) phaseMs.push_back(ms(((u.t - s.qpcVBlank) % P + P) % P));
    }

    // Frame-tick cadence (every frame is logged while telemetry is on, idle
    // 1.0x frames included, so a long interval is a real stall).
    std::vector<double> tickMs;
    uint64_t lateTicks = 0;
    for (uint32_t i = 1; i < n; ++i)
    {
        double d = ms(smp[i].qpcTick - smp[i - 1].qpcTick);
        tickMs.push_back(d);
        if (periodMs > 0 && d > 1.5 * periodMs) ++lateTicks;
    }

    // Duration of the MagSetFullscreenTransform call itself. A call longer than
    // a refresh period blocks the render loop and drops frames.
    std::vector<double> setMs;
    uint64_t setStalls = 0;
    for (const auto& u : upd)
    {
        double d = ms(u.tEnd - u.t);
        setMs.push_back(d);
        if (periodMs > 0 && d > periodMs) ++setStalls;
    }

    // Zoom anchor error (pointer source): desktop point under the cursor should
    // stay put. Desktop-px offset error x zoom = physical px on screen.
    std::vector<double> anchor, quant, deadzone;
    for (const auto& u : upd)
    {
        const TransformSample& s = smp[u.s];
        if (s.source != 0 || s.zoom <= 1.0f) continue;
        double z = s.zoom;
        anchor.push_back(std::hypot(s.idealOffX - s.appliedX, s.idealOffY - s.appliedY) * z);
        quant.push_back(std::hypot(s.offX - s.appliedX, s.offY - s.appliedY) * z);
        deadzone.push_back(std::hypot(s.idealOffX - s.offX, s.idealOffY - s.offY) * z);
    }

    // Round trip: zoom displayed at MARK 1 vs MARK 2.
    auto zoomAt = [&](int64_t t) -> double {
        double z = 1.0;
        for (const auto& u : upd) { if (u.t > t) break; z = smp[u.s].zoom; }
        return z;
    };
    json rt = json::array();
    double rtMax = 0.0;
    int64_t pendingBegin = 0;
    for (const auto& m : marks)
    {
        if (m.second == 1) pendingBegin = m.first;
        else if (m.second == 2 && pendingBegin)
        {
            double z0 = zoomAt(pendingBegin), z1 = zoomAt(m.first);
            double e = std::fabs(z1 / z0 - 1.0) * 100.0;
            rtMax = std::max(rtMax, e);
            rt.push_back({{"begin_zoom", z0}, {"end_zoom", z1}, {"error_pct", e}});
            pendingBegin = 0;
        }
    }

    double zMax = 1.0, zEnd = upd.empty() ? 1.0 : smp[upd.back().s].zoom;
    for (const auto& u : upd) zMax = std::max(zMax, double(smp[u.s].zoom));

    json j;
    j["schema"] = 1;
    j["trace"] = narrow(std::filesystem::path(s_tracePath).filename().wstring());
    j["label"] = narrow(s_label);
    j["completed"] = s_benchCompleted;
    j["computer"] = computerName();
    j["power"] = powerSource();
    j["uiaccess"] = processHasUIAccess();
    j["build"] = std::string(__DATE__) + " " + __TIME__;
    j["refresh_period_ms"] = periodMs;
    j["refresh_hz"] = periodMs > 0 ? 1000.0 / periodMs : 0.0;
    j["trace_meta"] = s_traceMeta;
    j["counts"] = {{"trace_events", s_trace.size()}, {"injected", s_injects.size()},
                   {"consumed_inputs", inputs.size()}, {"bursts", bursts.size()},
                   {"frames_logged", n}, {"frames_dropped", dropped},
                   {"updates", upd.size()}, {"updates_failed", failed}};
    j["update_interval_ms"] = stats(intervalMs);
    j["update_interval_periods"] = stats(intervalPeriods);
    j["refreshes"] = {{"active", activeRefreshes}, {"missed", missed},
                      {"missed_pct", activeRefreshes ? 100.0 * double(missed) / double(activeRefreshes) : 0.0},
                      {"multi_update", multi}};
    j["vblank_phase_ms"] = stats(phaseMs);
    j["tick_interval_ms"] = stats(tickMs);
    j["late_ticks"] = lateTicks;
    j["set_call_ms"] = stats(setMs);
    j["set_call_first_ms"] = setMs.empty() ? json(nullptr) : json(setMs.front());
    j["set_call_stalls"] = setStalls;
    j["anchor_error_px"] = stats(anchor);
    j["anchor_quantization_px"] = stats(quant);
    j["anchor_deadzone_px"] = stats(deadzone);
    j["roundtrip"] = rt;
    j["roundtrip_error_pct_max"] = rt.empty() ? json(nullptr) : json(rtMax);
    j["latency_ms"] = {{"input_to_update", stats(latUpdMs)}, {"input_to_next_vblank", stats(latVblMs)},
                       {"inputs_without_update", noUpdateInputs}};
    j["settle_ms"] = stats(settleMs);
    j["zoom_step_log"] = stats(stepLog);
    j["zoom"] = {{"max", zMax}, {"end", zEnd}};
    return j;
}

static void writeBenchOutputs()
{
    namespace fs = std::filesystem;
    const fs::path dir(s_outDir);
    const uint32_t n = std::min(s_telemetry.count.load(std::memory_order_acquire), s_telemetry.capacity);
    const TransformSample* smp = s_telemetry.samples;
    const double f = double(qpcFreq());
    auto rel = [&](int64_t q) { return q ? double(q - s_benchStartQpc) * 1000.0 / f : 0.0; };

    {
        std::ofstream o(dir / "frames.csv");
        o << "t_ms,set_begin_ms,set_end_ms,vblank_ms,c_refresh,period_ms,target_zoom,zoom,ideal_x,ideal_y,"
             "off_x,off_y,applied_x,applied_y,cursor_x,cursor_y,scroll_delta,source,mode,updated,ok,"
             "rb_zoom,rb_x,rb_y\n";
        char line[512];
        for (uint32_t i = 0; i < n; ++i)
        {
            const TransformSample& s = smp[i];
            // applied_x/y and rb_x/y are both the MagGetFullscreenTransform read-back.
            snprintf(line, sizeof(line),
                     "%.4f,%.4f,%.4f,%.4f,%llu,%.4f,%.6f,%.6f,%.3f,%.3f,%.3f,%.3f,%d,%d,%d,%d,%d,%u,%u,%u,%u,%.7f,%d,%d\n",
                     rel(s.qpcTick), rel(s.qpcSetBegin), rel(s.qpcSetEnd), rel(s.qpcVBlank),
                     (unsigned long long)s.cRefresh, double(s.qpcRefreshPeriod) * 1000.0 / f,
                     s.targetZoom, s.zoom, s.idealOffX, s.idealOffY, s.offX, s.offY,
                     s.appliedX, s.appliedY, s.cursorX, s.cursorY, s.scrollDelta,
                     s.source, s.mode, s.updated, s.ok, s.rbZoom, s.appliedX, s.appliedY);
            o << line;
        }
    }
    {
        std::ofstream o(dir / "events.csv");
        // action/device (appended): the gesture tracker's verdict for decided
        // events ("-" for MARK / CONTACT rows).
        o << "trace_index,trace_t_us,inject_ms,source,delta,mod_held,consumed,action,device\n";
        for (const auto& r : s_injects)
        {
            const TraceEvent& e = s_trace[r.traceIndex];
            char injectMs[32];   // fixed 0.1 us resolution (default stream precision is 6 significant digits)
            snprintf(injectMs, sizeof(injectMs), "%.4f", rel(r.qpc));
            o << r.traceIndex << ',' << e.tUs << ',' << injectMs << ',' << kindName(e.kind) << ','
              << e.delta << ',' << (r.modHeld ? 1 : 0) << ',' << (r.consumed ? 1 : 0) << ','
              << (!r.decided ? "-" : r.action == ScrollAction::Zoom ? "zoom"
                                   : r.action == ScrollAction::Swallow ? "swallow" : "pass") << ','
              << (!r.decided ? "-" : r.device == ScrollDevice::Touchpad ? "touchpad" : "mouse") << '\n';
        }
    }
    // Split warm-up frames from the measured run.
    uint32_t w = 0;
    while (w < n && smp[w].qpcTick < s_warmupEndQpc) ++w;
    double coldMax = 0.0, coldFirst = -1.0;
    for (uint32_t i = 0; i < w; ++i)
        if (smp[i].updated)
        {
            double d = double(smp[i].qpcSetEnd - smp[i].qpcSetBegin) * 1000.0 / f;
            coldMax = std::max(coldMax, d);
            if (coldFirst < 0) coldFirst = d;
        }
    json summary = computeSummary(smp + w, n - w, s_telemetry.dropped.load());
    summary["cold_start"] = {{"warmup", !s_cold},
                             {"first_set_call_ms", s_cold ? json(nullptr) : json(coldFirst)},
                             {"max_set_call_ms", s_cold ? json(nullptr) : json(coldMax)}};
    std::ofstream(dir / "summary.json") << summary.dump(2) << '\n';

    if (summary["counts"]["updates_failed"].get<size_t>() > 0 && exitCode() == kExitOk)
        setExitCode(kExitMagFailures);
}

int finish(bool /*renderStoppedClean*/)
{
    if (s_mode == Mode::Record)
    {
        InputInterceptor::setWheelTap(nullptr);
        recordDrain();
        if (s_recordFile)
        {
            fprintf(s_recordFile, "# events: %llu\n# dropped: %u\n", (unsigned long long)s_recordCount, s_ringDropped);
            fclose(s_recordFile);
            s_recordFile = nullptr;
        }
    }
    else if (s_mode == Mode::Bench)
    {
        if (!s_benchCompleted && exitCode() == kExitOk) setExitCode(kExitWatchdog);
        try { writeBenchOutputs(); }
        catch (const std::exception& ex)
        {
            SZ_LOG_ERROR("DevTools", L"Bench: writing outputs failed: %S", ex.what());
            if (exitCode() == kExitOk) setExitCode(kExitBadArgs);
        }
    }
    return exitCode();
}

} // namespace DevTools
} // namespace SmoothZoom

#endif // SMOOTHZOOM_DEV_TOOLS

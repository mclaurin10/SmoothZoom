#pragma once
// =============================================================================
// SmoothZoom — Dev tools (compiled only with SMOOTHZOOM_DEV_TOOLS=ON)
//
//   --record <file>             normal operation + log every wheel event the hook
//                               sees (and RAW/PTP accumulator writes) to a trace CSV
//   --bench <trace> --out <dir> replay a trace at its recorded timestamps from the
//                               main thread through the hook's decision logic;
//                               write frames.csv, events.csv, summary.json
//   --reset                     reset the system magnification (1.0x, input
//                               transform off, identity color, cursor shown), exit
//   --probe <file>              write the current fullscreen transform, exit
//   --timeout <sec>             watchdog: request a clean exit after <sec>
//   --selfkill-ms <ms>          TerminateProcess after <ms> (kill-mid-zoom test)
//   --no-cursor                 bench: do not move the cursor to trace positions
//   --settle-ms <ms>            bench: quiet time after the last event (default 750)
//
// Every dev-tools build also creates the named quit event
// Local\SmoothZoom.DevTools.Quit (signalled by tools\deploy.ps1) so a medium-IL
// script can ask the UIAccess process to exit cleanly. See Doc 3 §7.
// =============================================================================

#ifdef SMOOTHZOOM_DEV_TOOLS

#include "smoothzoom/common/DevTelemetry.h"
#include <string>

namespace SmoothZoom
{
struct SharedState;
struct SettingsSnapshot;

namespace DevTools
{

enum class Mode { Normal, Record, Bench, Reset, Probe };

// Exit codes (dev builds only; release builds keep their existing codes).
enum ExitCode : int
{
    kExitOk              = 0,
    kExitMagFailures     = 2,  // bench completed but some MagSetFullscreenTransform calls failed
    kExitWatchdog        = 3,  // timeout or quit event before the bench finished
    kExitForced          = 4,  // watchdog had to TerminateProcess
    kExitSelfKill        = 9,  // --selfkill-ms
    kExitAlreadyRunning  = 10,
    kExitMagInitFailed   = 11,
    kExitBadArgs         = 12,
    kExitConflict        = 13, // Magnify.exe running
    kExitHookFailed      = 14,
};

// Parse the process command line. Returns false (and fills err) on bad args.
bool parseCommandLine(std::wstring& err);
Mode mode();
// Bench mode suppresses modal dialogs and does not load/save config.json.
bool unattended();

// Called once early in WinMain (after the single-instance check). Creates the
// quit event and the watchdog thread. emergencyReset is invoked from the
// watchdog before a forced TerminateProcess.
void startWatchdog(unsigned long mainThreadId, int (*emergencyReset)());

// --reset / --probe: run and return the process exit code.
int runResetMode();
int runProbeMode();

// --bench: load the trace and allocate telemetry BEFORE the render thread
// starts. Returns false on a bad trace. Fills the modifier the trace was
// recorded with (0 = keep defaults) so main can apply it to settings.
bool prepareBench(int& traceModifierVK);
TransformTelemetry* benchTelemetry();

// --bench: replay on the main thread. pump() must drain the thread's message
// queue (hooks need it) and return false once WM_QUIT is seen.
void runBench(SharedState& state, bool (*pump)());

// --record: install the hook tap and start the periodic drain (main thread).
bool startRecord(SharedState& state);
// RAW/PTP accumulator writers in main.cpp report here (main thread).
void tapAccumulator(WheelSource source, int16_t delta);

// After the message loop exits and the render thread has stopped: flush the
// record file / write bench outputs. Returns the dev exit code to use.
int finish(bool renderStoppedClean);

void setExitCode(int code);
int exitCode();

} // namespace DevTools
} // namespace SmoothZoom

#endif // SMOOTHZOOM_DEV_TOOLS

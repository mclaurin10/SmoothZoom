# SmoothZoom

macOS-style full-screen magnification for Windows.

Native C++17 Win32 application using the Magnification API. Hold the Win key
and scroll to smoothly zoom the entire desktop with proportional viewport tracking.

## Prerequisites

- **Windows 10 1903+** (build 18362)
- **Visual Studio 2022** with C++ desktop workload and Windows SDK
- **CMake 3.20+**
- Self-signed code-signing certificate (see below)

## Quick Start

```
# 1. One-time: create dev signing cert (elevated PowerShell)
Set-ExecutionPolicy -Scope Process Bypass
.\scripts\dev_sign_setup.ps1

# 2. Build (from x64 Native Tools Command Prompt for VS 2022)
scripts\build.bat

# 3. Sign the binaries
.\scripts\sign-binary.ps1

# 4. Install to secure folder (elevated PowerShell)
.\scripts\install-secure.ps1

# 5. Run
& "C:\Program Files\SmoothZoom\SmoothZoom.exe"
```

## Build

```
# From x64 Native Tools Command Prompt for VS 2022:
scripts\build.bat

# Or manually:
mkdir build && cd build
cmake -G "Visual Studio 17 2022" -A x64 ..
cmake --build . --config Debug --parallel
```

## Development Signing (R-12)

The Magnification API requires `uiAccess="true"`, which requires the binary to be
signed and running from a secure folder (`C:\Program Files\`).

```powershell
# One-time setup (elevated PowerShell):
.\scripts\dev_sign_setup.ps1

# After each build — sign all binaries:
.\scripts\sign-binary.ps1

# Deploy to secure folder (elevated PowerShell):
.\scripts\install-secure.ps1
```

## Phase 0 — Risk Spike

Phase 0 validates three assumptions before building the full application:

1. **Float-precision magnification** — `MagSetFullscreenTransform` honors
   sub-integer float values (e.g., 1.07, 2.34) and produces visually smooth results.
2. **Sub-frame API latency** — Visual changes appear within one VSync of the API call.
3. **Reliable scroll interception** — A `WH_MOUSE_LL` hook from a `uiAccess="true"`
   process intercepts scroll events system-wide, including over elevated windows.

### Running the Phase 0 Harness

```
# After build + sign + install:
& "C:\Program Files\SmoothZoom\Phase0Harness.exe"
```

The harness:
- Runs automated E0.1 (1.5× static zoom) and E0.2 (smooth ramp) tests on startup
- Then enters interactive mode: **Hold Win + Scroll** to zoom, **Ctrl+Q** to exit

### Running the Full Application (Phase 0 Slice)

```
& "C:\Program Files\SmoothZoom\SmoothZoom.exe"
```

Controls:
- **Hold Win + Scroll wheel** — Zoom in/out
- **Release Win** — Retain current zoom, scroll passes through normally
- **Ctrl+Q** — Reset to 1.0× and exit

### Phase 0 Exit Criteria

| # | Criterion | How to Test |
|---|-----------|-------------|
| E0.1 | 1.5× produces visible magnification between 1× and 2× | Harness runs this automatically |
| E0.2 | 0.01-increment ramp produces smooth zoom | Harness runs this automatically |
| E0.3 | Zoom changes appear within one frame | Observe during E0.2 ramp |
| E0.4 | Win+Scroll zooms the screen | Interactive: hold Win, scroll up/down |
| E0.5 | Scroll without Win reaches foreground app | Release Win, scroll in a browser |
| E0.6 | Hook works over elevated windows | Open Task Manager as admin, Win+Scroll over it |

**If E0.1 or E0.2 fails:** Stop — the Magnification API approach may be invalid. See R-02 in Doc 5.

## Unit Tests

```
cd build
cmake --build . --config Debug --target smoothzoom_tests
ctest -C Debug
```

Tests cover pure logic components (ZoomController, ViewportTracker, WinKeyManager)
with no Win32 API dependencies — safe to run on any machine including CI.

## Architecture

Ten components across four layers:

```
Input Layer:   InputInterceptor · WinKeyManager · FocusMonitor · CaretMonitor
Logic Layer:   ZoomController · ViewportTracker · RenderLoop
Output Layer:  MagBridge
Support Layer: SettingsManager · TrayUI
```

Three threads:
- **Main Thread:** Message pump, low-level hooks, TrayUI
- **Render Thread:** VSync-locked frame ticks via DwmFlush(), calls MagBridge
- **UIA Thread:** (Phase 3) UI Automation focus/caret subscriptions

See `docs/` for the five design documents and research report.

## Phased Delivery

| Phase | Name | Status |
|-------|------|--------|
| **0** | **Foundation & Risk Spike** | **Current** |
| 1 | Core Scroll-Gesture Zoom | Planned |
| 2 | Keyboard Shortcuts & Animation | Planned |
| 3 | Accessibility Tracking | Planned |
| 4 | Temporary Toggle | Planned |
| 5 | Settings, Tray, Persistence | Planned |
| 6 | Polish & Hardening | Planned |

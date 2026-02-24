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

## Usage

```
& "C:\Program Files\SmoothZoom\SmoothZoom.exe"
```

### Controls

- **Win + Scroll wheel** — Zoom in/out (continuous)
- **Win + Plus / Minus** — Zoom in/out (animated step)
- **Win + Esc** — Reset to 1× (animated)
- **Ctrl+Alt (hold)** — Temporary toggle (peek at zoom/unzoom)
- **Win+Ctrl+M** — Toggle zoom on/off
- **Tray icon** — Right-click for settings and exit

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
| 0 | Foundation & Risk Spike | Done |
| 1 | Core Scroll-Gesture Zoom | Done |
| 2 | Keyboard Shortcuts & Animation | Done |
| 3 | Accessibility Tracking | Done |
| 4 | Temporary Toggle | Done |
| **5** | **Settings, Tray, Persistence** | **Current** (5A/5B done) |
| 6 | Polish & Hardening | Planned |

# SmoothZoom

Full-screen magnification for Windows, inspired by macOS Accessibility Zoom. Hold Win+Scroll to smoothly zoom the entire desktop up to 10× — the viewport follows your cursor naturally with fluid, pointer-centered tracking. No stepped increments, no jarring jumps. Native C++17, pure Win32, using the Windows Magnification API.

## Current Status

**Phase 6 — Polish & Hardening** (in progress). All core features are implemented and functional. Testing and stabilization underway.

## Key Features

- **Scroll-gesture zoom** — Hold Win and scroll to zoom in/out continuously
- **Pointer-centered viewport tracking** — The magnified view follows your cursor proportionally across the desktop
- **Keyboard shortcuts** — Win+Plus/Minus for animated step zoom, Win+Esc to reset
- **Animated transitions** — Ease-out zoom animations with retargeting and scroll-interrupts-animation
- **Focus following** — Viewport tracks keyboard focus changes via UI Automation
- **Caret following** — Viewport tracks the text cursor during active typing
- **Temporary toggle** — Hold Ctrl+Alt to peek at zoom/unzoom, release to restore
- **Settings persistence** — JSON config file with hot-reload
- **System tray icon** — Right-click for settings window and exit
- **Color inversion** — Accessibility color inversion mode
- **Multi-monitor support** — Basic multi-monitor awareness
- **Crash recovery** — Exception handler resets zoom; dirty-shutdown sentinel detection

## Controls

| Shortcut | Action |
|----------|--------|
| Win + Scroll wheel | Zoom in/out (continuous) |
| Win + Plus / Minus | Zoom in/out (animated step) |
| Win + Esc | Reset to 1× (animated) |
| Ctrl+Alt (hold) | Temporary toggle (peek at zoom/unzoom) |
| Win+Ctrl+M | Toggle zoom on/off |
| Tray icon (right-click) | Settings and exit |

## Build Requirements

- **Windows 10 1903+** (build 18362)
- **Visual Studio 2022** with C++ desktop workload and Windows SDK
- **CMake 3.20+**
- **x64 only** — Magnification API is unsupported under WOW64
- **UIAccess manifest** — Binary must declare `uiAccess="true"`
- **Code signing** — Binary must be signed and run from a secure folder (e.g., `C:\Program Files\SmoothZoom\`)

## How to Build and Run

```powershell
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

Or use the all-in-one deploy script:

```powershell
.\scripts\deploy.ps1
```

### Unit Tests

```
cd build
ctest -C Debug
```

Tests cover pure logic components (ZoomController, ViewportTracker, WinKeyManager, ModifierUtils) with no Win32 API dependencies — safe to run on any machine including CI.

## Architecture Overview

Ten components across four layers, running on four threads:

```
Input Layer:   InputInterceptor · WinKeyManager · FocusMonitor · CaretMonitor
Logic Layer:   ZoomController · ViewportTracker · RenderLoop
Output Layer:  MagBridge
Support Layer: SettingsManager · TrayUI
```

| Thread | Responsibility |
|--------|---------------|
| **Main** | Message pump, low-level hooks, TrayUI, app lifecycle |
| **Render** | VSync-locked frame ticks via `DwmFlush()`, calls MagBridge |
| **UIA** | UI Automation focus subscriptions (isolated COM apartment) |
| **Caret** | `GetGUIThreadInfo` polling at ~30 Hz (separate from UIA — UIA caret events are unreliable) |

Communication between threads uses atomics, SeqLock, lock-free queues, and atomic pointer swap — no mutexes on the render hot path.

See `docs/` for the five design documents covering scope, behavior specification (139 acceptance criteria), technical architecture, phased delivery plan, and risk mitigations.

## Known Limitations

- **Image smoothing toggle deferred** — `MagSetFullscreenTransform` provides no filtering parameter. Nearest-neighbor mode (AC-2.3.08) and the toggle (AC-2.3.09) depend on a future Desktop Duplication API migration (R-01).
- **CPU usage** — May exceed target in some scenarios; optimization ongoing in Phase 6.
- **Windows Magnifier conflict** — Only one full-screen magnifier can run at a time. SmoothZoom detects the native Magnifier and advises the user.
- **Secure desktop inaccessible** — Magnification API does not work on Ctrl+Alt+Delete, UAC prompts, or the lock screen.

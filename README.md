# SmoothZoom

macOS-style full-screen magnification for Windows.

Native C++17 Win32 application using the Magnification API. Hold a modifier key
and scroll to smoothly zoom the entire desktop with proportional viewport tracking.

## Prerequisites

- **Windows 10 1903+** (build 18362)
- **Visual Studio 2022** with C++ desktop workload and Windows SDK
- **CMake 3.20+**
- Self-signed code-signing certificate (see `scripts/dev_sign_setup.ps1`)

## Build (Windows)

```
# From x64 Native Tools Command Prompt for VS 2022:
scripts\build.bat

# Or manually:
mkdir build && cd build
cmake -G "Visual Studio 17 2022" -A x64 ..
cmake --build . --config Debug
```

## Development Signing

The Magnification API requires `uiAccess="true"`, which requires the binary to be
signed and installed in a secure location. For development:

```powershell
# Run once from elevated PowerShell:
.\scripts\dev_sign_setup.ps1

# Then sign each build:
signtool sign /n "SmoothZoom Dev" /fd SHA256 build\Debug\SmoothZoom.exe

# Copy to secure location:
copy build\Debug\SmoothZoom.exe "C:\Program Files\SmoothZoom\"
```

## Architecture

Ten components across four layers — see `docs/` for full design documents.

```
Input Layer:   InputInterceptor · WinKeyManager · FocusMonitor · CaretMonitor
Logic Layer:   ZoomController · ViewportTracker · RenderLoop
Output Layer:  MagBridge
Support Layer: SettingsManager · TrayUI
```

## Phased Delivery

| Phase | Name                        |
|-------|-----------------------------|
| 0     | Foundation & Risk Spike     |
| 1     | Core Scroll-Gesture Zoom    |
| 2     | Keyboard Shortcuts & Anim.  |
| 3     | Accessibility Tracking      |
| 4     | Temporary Toggle            |
| 5     | Settings, Tray, Persistence |
| 6     | Polish & Hardening          |

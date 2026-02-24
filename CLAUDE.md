# SmoothZoom

Native C++17 Win32 full-screen magnifier for Windows. Hold Win+Scroll to smoothly zoom up to 10× with continuous viewport tracking of pointer, keyboard focus, and text cursor. Uses the Windows Magnification API.

## Design Documents — Read Before Implementing

Five docs and a research report fully specify this project. **Always consult the relevant doc before coding. Do not guess—look it up.**

| Doc | File | Covers |
|-----|------|--------|
| 1 — Scope | `01_Project_Scope_and_Non-Scope.md` | In/out of scope, success criteria, constraints, settings |
| 2 — Behavior Spec | `02_Behavior_Specification.md` | 139 acceptance criteria (AC-numbered). The authority on behavior. |
| 3 — Architecture | `03_Technical_Architecture.md` | Components, threading, data flow, pseudocode, build/deploy |
| 4 — Phases | `04_Phased_Delivery_Plan.md` | Seven phases (0–6), exit criteria, AC-to-phase mapping |
| 5 — Risks | `05_Technical_Risks_and_Mitigations.md` | 22 risks (R-01–R-22) with mitigations |

Cite **AC numbers** for behavior (e.g., AC-2.1.06), **risk IDs** for technical concerns (e.g., R-05), and **exit criteria** for phase gates (e.g., E1.4).

## Architecture

Single-process, four layers, ten components:

- **Input Layer:** InputInterceptor (hooks), WinKeyManager (Win key + Start Menu suppression), FocusMonitor (UIA focus), CaretMonitor (UIA caret + GTTI polling)
- **Logic Layer:** ZoomController (zoom state/animation), ViewportTracker (offset + source priority), RenderLoop (frame tick engine)
- **Output Layer:** MagBridge (sole Magnification API wrapper — no other component calls Mag* functions)
- **Support Layer:** SettingsManager (config.json + snapshots), TrayUI (tray icon + settings window)

## Threading Model — Three Threads

**Main Thread:** Message pump, low-level hooks, TrayUI, app lifecycle.
**Render Thread:** VSync-locked frame ticks via `DwmFlush()`, message pump for Mag API internals, calls MagBridge.
**UIA Thread:** UI Automation subscriptions (focus + caret), isolated COM apartment.

### Threading Invariants

These are the rules most likely to cause subtle, hard-to-diagnose bugs if violated:

1. **Hook callbacks must be minimal.** Read event, update an atomic or post a message, return. No computation, no I/O, no allocation. The system silently deregisters hooks that exceed ~300ms. (R-05)
2. **Render thread: no heap allocation, no mutexes, no I/O on the per-frame hot path.** All data comes from pre-allocated shared state via atomics or SeqLock.
3. **Render thread must pump messages.** `MagSetFullscreenTransform` offsets are silently ignored without a `PeekMessage`/`DispatchMessage` loop on the calling thread. The zoom factor applies but the viewport stays at (0,0). This is undocumented Win32 behavior.
4. **UIA thread is isolated.** Slow UIA providers (Java, some Electron apps) must never block hooks or drop frames. Timeout bounding-rect queries at 100ms. (R-11)
5. **`MagSetInputTransform` and `MagSetFullscreenTransform` must be called in the same frame tick, same code block, with identical zoom/offset values.** Deferring one to a later frame causes click-targeting bugs. (R-04)
6. **Settings use atomic pointer swap.** Immutable snapshot struct, copy-on-write. Readers never lock.

### Shared State Mechanisms

- **Atomics:** modifier key state, scroll delta accumulator (exchange-with-0), timestamps
- **SeqLock:** pointer position, focus rectangle, caret rectangle (small structs, infrequent writes, frequent reads)
- **Lock-free queue:** keyboard commands (Main → Render)
- **Atomic pointer swap:** settings snapshots (Main → All)

## Hard Constraints

Violating these causes silent failure or a broken build. Non-negotiable.

1. **UIAccess + signed binary.** Manifest must declare `uiAccess="true"`. Binary must be signed (self-signed OK for dev) and run from a secure location. Without this, `MagSetFullscreenTransform` silently returns FALSE. (R-12)
2. **x64 only.** Magnification API unsupported under WOW64.
3. **PerMonitorV2 DPI awareness** in manifest. Without it, coordinates are virtualized and viewport math breaks on mixed-DPI setups.
4. **Single magnifier limit.** Detect native Magnifier and advise user. (AC-ERR.01)
5. **Secure desktop inaccessible.** Magnification API doesn't work on Ctrl+Alt+Delete, UAC, lock screen.
6. **No external frameworks.** Pure Win32 + C++17 + MSVC. Only optional external dep: nlohmann/json (header-only).
7. **Windows 10 1903+** minimum platform target.

## Known Issues & Debugging

1. **Viewport stuck at (0,0):** If zoom works but the view doesn't follow the cursor, the render thread's message pump is missing or broken. The Magnification API requires a `PeekMessage`/`DispatchMessage` loop on the calling thread for `MagSetFullscreenTransform` offsets to take effect. This is undocumented — the API returns TRUE and `MagGetFullscreenTransform` reads back the correct values, but DWM does not apply the offsets without processing internal messages. See `render-loop.md` and `mag-bridge.md` rules for details.

2. **Diagnostic strategy for Mag API issues:** Add a temporary `MagGetFullscreenTransform` read-back in `MagBridge::setTransform()` to verify the API stored the requested values. Log both set and get values via `OutputDebugStringW` (viewable in DebugView or VS Output). Remove after verification.

## Phased Delivery — Follow Strictly

Do not implement later-phase features prematurely. Each phase produces a runnable, testable build. Check Doc 4 for full exit criteria and AC coverage per phase.

| Phase | Name | Key Delivery | Status |
|-------|------|-------------|--------|
| 0 | Foundation & Risk Spike | Test harness validating API float precision, latency, hook reliability | Done |
| 1 | Core Scroll-Gesture Zoom | Smooth Win+Scroll zoom, proportional viewport tracking, Start Menu suppression | Done |
| 2 | Keyboard Shortcuts & Animation | Win+Plus/Minus/Esc with ease-out, target retargeting, scroll-interrupts-animation | Done |
| 3 | Accessibility Tracking | UIA thread, focus following, caret following, source priority arbitration | Done |
| 4 | Temporary Toggle | Ctrl+Alt hold-to-peek, bidirectional, state preservation | Done |
| 5 | Settings, Tray, Persistence | config.json, settings window, tray icon, configurable modifier/shortcuts | Current (5A/5B done) |
| 6 | Polish & Hardening | Color inversion, multi-monitor, crash recovery, conflict detection, perf audit | Planned |

## Component Boundaries

Each component has a single responsibility. No component reaches into another's internals. Communication only through shared state or defined interfaces.

- **MagBridge** is the only file that `#include`s `Magnification.h`. This isolates a future migration to Desktop Duplication API. (R-01)
- **WinKeyManager** is factored out of InputInterceptor for testability of the Win key state machine.
- **InputInterceptor** never consumes keyboard events (only observes). It consumes scroll events only when the modifier is held.
- **ViewportTracker** arbitrates between sources: active typing → Caret; recent focus change (after 100ms debounce) → Focus; otherwise → Pointer. Transitions between sources animate over 200ms.
- **ZoomController** has four modes: IDLE, SCROLL_DIRECT, ANIMATING, TOGGLING. Uses `double` internally for zoom math, converts to `float` only at the API boundary. Snaps to 1.0× within epsilon 0.005. (R-17)

## Top Risks Affecting Daily Coding

1. **R-05 (Hook Deregistration)** — Will happen. Watchdog timer every 5s detects and reinstalls. Keep hook callbacks absolutely minimal.
2. **R-04 (Input Transform Desync)** — Same-frame API calls are the only defense. Test click accuracy at 5× zoom across screen positions.
3. **R-14 (Crash Leaves Screen Magnified)** — Install `SetUnhandledExceptionFilter` to reset zoom. Write dirty-shutdown sentinel file. Handle `WM_ENDSESSION`.
4. **R-01 (API Deprecation)** — MagBridge isolation bounds the migration. Monitor Windows Insider builds.
5. **R-09 (UIA Inconsistency)** — Validate every bounding rectangle from UIA. Reject zero-area, negative, off-screen. Degrade gracefully.

## Build Requirements

- C++17, MSVC (VS 2022+), CMake or MSBuild, x64 only.
- Link: `Magnification.lib`, `Dwmapi.lib`, `UIAutomationCore.lib`, `User32.lib`, `Shell32.lib`.
- All builds signed. Self-signed cert installed to Trusted Root CA for dev.
- Run from secure folder (e.g., `C:\Program Files\SmoothZoom\`).

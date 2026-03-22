# SmoothZoom — Project-Wide Constraints

## Current Phase: Transitioning from Phase 0 → Phase 1

Phase 0 (Foundation & Risk Spike) validated float-precision magnification, sub-frame API latency, and reliable scroll interception. The test harness and basic SmoothZoom.exe exist with Win+Scroll zoom and Ctrl+Q exit.

**Phase 1 scope — what to build now:**
- MagBridge (full), InputInterceptor (full mouse hook, keyboard hook limited to modifier observation), WinKeyManager (full), ZoomController (SCROLL_DIRECT + IDLE modes only), ViewportTracker (pointer-tracking only), RenderLoop (full frame-tick loop)
- Shared state: scroll delta accumulator (atomic), pointer position (atomic pair / SeqLock), modifier key state (atomic)
- ACs covered: AC-2.1.01–AC-2.1.18 (scroll-gesture zoom + Start Menu suppression), AC-2.2.01–AC-2.2.03 + AC-2.2.10 (scroll smoothness), AC-2.3.01–AC-2.3.13 (magnification quality), AC-2.4.01–AC-2.4.13 (viewport tracking), AC-ERR.02–AC-ERR.04 (partial error handling)
- Exit criteria: E1.1 through E1.14 (see Doc 4 §4.1.4)

## Phase Gating — Do NOT Implement Early

| Feature | Earliest Phase | Key Indicator |
|---------|---------------|---------------|
| Keyboard shortcuts (Win+Plus/Minus/Esc) | 2 | ZoomController ANIMATING mode, ease-out interpolation |
| Keyboard animation engine, target retargeting | 2 | AC-2.2.04 through AC-2.2.09 |
| UIA thread, FocusMonitor, CaretMonitor | 3 | AC-2.5.*, AC-2.6.* |
| ViewportTracker multi-source priority arbitration | 3 | `determineActiveSource()`, source transition smoothing |
| Temporary toggle (Ctrl+Alt hold-to-peek) | 4 | ZoomController TOGGLING mode, AC-2.7.* |
| SettingsManager, TrayUI, config.json | 5 | AC-2.9.*, configurable modifier keys |
| Color inversion (`MagSetFullscreenColorEffect`) | 6 | AC-2.10.* |
| Multi-monitor (`MonitorFromPoint`, `WM_DISPLAYCHANGE`) | 6 | AC-MM.* |
| Conflict detection (Magnify.exe check) | 6 | AC-ERR.01 |
| Crash recovery (sentinel file, `SetUnhandledExceptionFilter`) | 6 | R-14 full mitigation |

If you find yourself writing code for a feature in the right column, stop and check the phase. Stub interfaces are acceptable if a current-phase component needs the signature for compilation, but do not implement the logic.

## Architecture — Ten Components, Four Layers

```
Input:   InputInterceptor · WinKeyManager · FocusMonitor(P3) · CaretMonitor(P3)
Logic:   ZoomController · ViewportTracker · RenderLoop
Output:  MagBridge
Support: SettingsManager(P5) · TrayUI(P5)
```

Each component has a single responsibility. Do not merge components or move responsibilities across boundaries. If a new behavior doesn't fit an existing component, that's a design conversation, not a reason to stuff it into the nearest file.

## Threading Model — Three Threads, Strict Affinity

| Thread | Owns | Key Constraint |
|--------|------|----------------|
| **Main** | Message pump, hook callbacks (WH_MOUSE_LL, WH_KEYBOARD_LL), TrayUI | Hook callbacks must be minimal: read struct, atomic write, return. No computation, no I/O, no COM, no blocking. |
| **Render** | RenderLoop frame tick, all MagBridge calls | No heap allocation, no mutex, no I/O, no blocking calls except DwmFlush(). |
| **UIA** (Phase 3+) | FocusMonitor, CaretMonitor, COM/UIA subscriptions | Isolated so slow UIA callbacks cannot stall hooks or drop frames. |

**Thread communication uses only:** atomics, SeqLock (small structs), lock-free queue (commands), copy-on-write with atomic pointer swap (settings). No `std::mutex`, no `CRITICAL_SECTION`, no `WaitForSingleObject` on any hot path.

## Cross-Cutting Invariants

1. **No `#include <Magnification.h>` outside MagBridge.** All Mag* calls go through MagBridge's public interface. This isolation bounds a future Desktop Duplication migration to one file. (R-01)

2. **No heap allocation on hot paths.** This applies to both hook callbacks (Main thread) and `frameTick()` (Render thread). No `new`, `malloc`, `std::vector::push_back`, `std::string` construction, or STL container mutation.

3. **No per-frame logging.** Log only on state transitions (zoom started/ended, source changed, error detected, hook re-registered). At 144Hz, per-frame logging is 144 writes/second.

4. **`GetCursorPos()` for pointer position in RenderLoop, not SharedState.** The low-level mouse hook's `WM_MOUSEMOVE` is unreliable under fullscreen magnification. `GetCursorPos()` is a fast shared-memory read (~1µs) that always returns the true position.

5. **`MagSetInputTransform` is NOT called per-frame.** With proportional mapping (`offset = pos * (1 - 1/zoom)`), input transform is mathematically redundant and causes a feedback loop with `GetCursorPos()`. Only `MagSetFullscreenTransform` is called per-frame. `MagSetInputTransform(FALSE)` is called only during shutdown.

6. **Use `double` for internal zoom math, `float` only at the API boundary.** Prevents floating-point accumulation drift over long sessions. Snap to exactly 1.0 when within epsilon (0.005). (R-17)

7. **Integer division in viewport math is a bug.** `screenW / zoom` must be floating-point division. Integer truncation causes click-offset errors that worsen at higher zoom levels.

## Build Constraints

- **C++17 / MSVC / x64 only.** No external frameworks, no Boost, no Qt. Win32 API + STL only. nlohmann/json (header-only, vendored in third_party/) is the sole exception.
- **UIAccess = true** in the application manifest. Binary must be signed and run from a secure folder (`C:\Program Files\SmoothZoom\`).
- **Build:** `scripts\build.bat` or CMake directly. Sign: `scripts\sign-binary.ps1`. Install: `scripts\install-secure.ps1`.
- **Unit tests** cover pure logic (ZoomController, ViewportTracker, WinKeyManager) with no Win32 API dependencies. Run via `ctest -C Debug` from the build directory.

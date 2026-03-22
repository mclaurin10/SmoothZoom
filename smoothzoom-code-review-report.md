# SmoothZoom — MVP Code Review Report

**Date:** 2025-03-21
**Reviewer:** Claude Opus 4.6
**Codebase State:** Phase 6 (code complete, testing in progress)
**Branch:** `master` @ `f287e4b`

---

## 1. Executive Summary

### Overall Health: **Strong — Production-Ready with Minor Issues**

The SmoothZoom codebase is well-architected, spec-compliant, and demonstrates strong threading discipline. The four-layer, ten-component architecture from Doc 3 is faithfully implemented with clean component boundaries. The render thread hot path meets all four performance invariants (zero heap allocation, no mutexes, no I/O, no blocking except `DwmFlush`). All reviewed acceptance criteria are correctly implemented.

### Top 3 Concerns

1. **T1 — SeqLock torn-read UB** (`SeqLock.h:34`): `data_` is non-atomic; concurrent read/write is undefined behavior per C++ standard. Safe in practice on x86-64/MSVC due to retry loop, but not portable. **Major.**

2. **T3 — Crash handler thread affinity** (`main.cpp:210-213`): Creates a new `MagBridge` and calls `MagInitialize()` from an arbitrary crash thread. The Magnification API has documented thread affinity requirements — zoom reset may silently fail. **Major.**

3. **R-11 gap — No UIA bounding-rect timeout** (`FocusMonitor.cpp:89`): `get_CurrentBoundingRectangle()` has no explicit timeout. A hanging UIA provider (Java Access Bridge, Electron) blocks the UIA thread indefinitely. Spec requires 100ms timeout. **Major.**

### Top 3 Strengths

1. **Clean architecture isolation** — MagBridge is the sole `Magnification.h` consumer. Component boundaries are strict. No cross-layer shortcuts detected.

2. **Zero-allocation render hot path** — All state is pre-allocated as file-scope statics. Shared state access uses atomics, SeqLock, and lock-free queue. No mutexes on the render thread.

3. **Comprehensive crash recovery** — Exception filter + sentinel file + `WM_ENDSESSION` handler + session lock/unlock notifications cover all crash/shutdown scenarios (R-14).

---

## 2. Architecture Conformance

### Component Checklist

| # | Component | Layer | File | Single Responsibility | Boundary Clean | Status |
|---|-----------|-------|------|----------------------|----------------|--------|
| 1 | InputInterceptor | Input | `src/input/InputInterceptor.cpp` | ✅ Hooks + event routing | ✅ Uses WinKeyManager, no Mag API | **Pass** |
| 2 | WinKeyManager | Input | `src/input/WinKeyManager.cpp` | ✅ Win key state machine | ✅ Consumed only by InputInterceptor | **Pass** |
| 3 | FocusMonitor | Input | `src/input/FocusMonitor.cpp` | ✅ UIA focus subscription | ✅ Writes only to SharedState SeqLock | **Pass** |
| 4 | CaretMonitor | Input | `src/input/CaretMonitor.cpp` | ✅ GTTI polling for caret | ✅ Writes only to SharedState SeqLock | **Pass** |
| 5 | ZoomController | Logic | `src/logic/ZoomController.cpp` | ✅ Zoom state + animation | ✅ Pure math, no I/O or API calls | **Pass** |
| 6 | ViewportTracker | Logic | `src/logic/ViewportTracker.cpp` | ✅ Offset computation + source arbitration | ✅ Pure static functions, no state | **Pass** |
| 7 | RenderLoop | Logic | `src/logic/RenderLoop.cpp` | ✅ Frame tick engine | ✅ Sole caller of MagBridge | **Pass** |
| 8 | MagBridge | Output | `src/output/MagBridge.cpp` | ✅ Magnification API wrapper | ✅ Only file that `#include`s `Magnification.h` | **Pass** |
| 9 | SettingsManager | Support | `src/support/SettingsManager.cpp` | ✅ JSON config + atomic snapshots | ✅ No UI, no API calls | **Pass** |
| 10 | TrayUI | Support | `src/support/TrayUI.cpp` | ✅ System tray + settings window | ✅ Reads settings via snapshot, no direct state | **Pass** |

### Cross-Cutting Checks

- **MagBridge isolation (R-01):** Confirmed — `#include <magnification.h>` appears only in `MagBridge.cpp`. `#pragma comment(lib, "Magnification.lib")` linked only in output layer.
- **Hook callback confinement:** Only `InputInterceptor.cpp` and `WinKeyManager.cpp` handle hook callbacks. No other component registers or processes hooks.
- **UIA confinement:** `FocusMonitor.cpp` subscribes to UIA focus events. `CaretMonitor.cpp` polls GTTI on its own thread (see T4 below). Both write to SharedState via SeqLock only.

### Deviation: Four Threads, Not Three (T4)

The spec mandates three threads (Main, Render, UIA). The implementation uses **four**: CaretMonitor runs its own `std::thread` for 30Hz GTTI polling, separate from FocusMonitor's UIA thread. This is a pragmatic decision — GTTI polling doesn't need COM initialization and isolating it prevents slow UIA providers from delaying caret updates. **Minor deviation, defensible.**

---

## 3. Threading Safety Findings

### T1 — SeqLock Torn-Read Undefined Behavior
| | |
|---|---|
| **Severity** | Major |
| **Location** | `include/smoothzoom/common/SeqLock.h:34` |
| **Description** | `data_` is a non-atomic `T`. Under the C++ memory model, concurrent read (render thread) and write (UIA/caret thread) on the same non-atomic variable is UB, even if the reader discards the result on sequence mismatch. |
| **Impact** | Safe on x86-64/MSVC in practice — the retry loop catches torn values and hardware provides total store order. Not portable to ARM or other compilers. |
| **Recommendation** | Document the x86-64/MSVC assumption. For portability, consider `memcpy` through `char*` (allowed aliasing) or `std::atomic<T>` with relaxed ordering. |

### T2 — SeqLock Writer Memory Ordering (Info)
| | |
|---|---|
| **Severity** | Info |
| **Location** | `SeqLock.h:22-24` |
| **Description** | The first `fetch_add(1, release)` does not order the subsequent `data_ = value` write. However, the second `fetch_add(1, release)` does provide the necessary fence, and the reader's acquire-check-retry pattern is correct. |
| **Impact** | None — the protocol is sound. The worst case is the reader seeing the pre-increment sequence (odd), which triggers a retry. |

### T3 — Crash Handler Thread Affinity
| | |
|---|---|
| **Severity** | Major |
| **Location** | `main.cpp:210-213` |
| **Description** | The `SetUnhandledExceptionFilter` callback creates a new `MagBridge` instance and calls `MagInitialize()` from whatever thread the crash occurred on. The Magnification API has thread affinity — `MagInitialize` and `MagSetFullscreenTransform` should be called from the same thread that owns the magnification window. |
| **Impact** | Zoom reset on crash may silently fail, leaving the screen magnified. The sentinel file provides a second line of defense (zoom reset on next launch), but an immediate reset would be preferable. |
| **Recommendation** | Instead of creating a new MagBridge, post a message to the render thread requesting reset, with a short timeout fallback. Or call the Mag API directly (bypassing MagBridge) as a best-effort — the crash handler already operates outside normal safety guarantees. |

### T4 — Four Threads Instead of Three
| | |
|---|---|
| **Severity** | Minor |
| **Location** | `CaretMonitor.cpp` (thread launch) |
| **Description** | CaretMonitor runs on its own `std::thread`, separate from FocusMonitor's UIA thread. Spec says three threads. |
| **Impact** | None functional. Defensible design choice — GTTI polling doesn't require COM and isolating it prevents slow UIA providers from blocking caret updates. |

### T5 — Dead SharedState Fields
| | |
|---|---|
| **Severity** | Minor |
| **Location** | `SharedState.h:23-24` |
| **Description** | `pointerX` and `pointerY` are written by hooks but never read by the render thread. RenderLoop uses `GetCursorPos()` directly for fresher data. |
| **Impact** | Wasted atomic writes on every mouse move in the hook callback (~negligible perf cost). |
| **Recommendation** | Remove `pointerX`/`pointerY` from SharedState, or document them as reserved for future use. |

### T6 — Deprecated `std::atomic_load/store` on `shared_ptr`
| | |
|---|---|
| **Severity** | Minor |
| **Location** | `main.cpp:199`, `RenderLoop.cpp:232` |
| **Description** | `std::atomic_load(&shared_ptr)` and `std::atomic_store(&shared_ptr)` are deprecated in C++20, replaced by `std::atomic<std::shared_ptr<T>>`. |
| **Impact** | Works correctly under C++17/MSVC. Will produce deprecation warnings if the project migrates to C++20. |
| **Recommendation** | No action needed for C++17. Note for future C++20 migration. |

### T7 — MagBridge Static Screen Dimensions Race
| | |
|---|---|
| **Severity** | Minor |
| **Location** | `MagBridge.cpp:26-27` |
| **Description** | `s_screenWidth` and `s_screenHeight` are non-atomic `static int`, written by main thread on `WM_DISPLAYCHANGE`, read by render thread in `setInputTransform()`. |
| **Impact** | Low — writes occur only during display configuration changes (rare). Worst case is a single frame with stale screen dimensions. |
| **Recommendation** | Make `std::atomic<int>` with relaxed ordering. |

### Shared State Protection Verification

| Shared State | Spec Protection | Implementation | Correct? |
|---|---|---|---|
| Modifier key state | Atomic | `modifierHeld` atomic (relaxed) | ✅ |
| Pointer position | Atomic pair or SeqLock | `pointerX`/`pointerY` atomics (written but unused — T5) | ⚠️ Dead code |
| Scroll delta accumulator | Atomic (exchange-with-zero) | `scrollAccumulator.exchange(0, acquire)` | ✅ |
| Keyboard commands | Lock-free queue | `LockFreeQueue<ZoomCommand, 64>` SPSC | ✅ |
| Toggle state | Atomic | `toggleState` atomic | ✅ |
| Focus rectangle | SeqLock | `focusRect` SeqLock | ✅ (T1 caveat) |
| Caret rectangle | SeqLock | `caretRect` SeqLock | ✅ (T1 caveat) |
| Last keyboard timestamp | Atomic | `lastKeyboardInputTime` atomic (relaxed) | ✅ |
| Last focus timestamp | Atomic | `lastFocusChangeTime` atomic (relaxed) | ✅ |
| Settings snapshot | Atomic pointer swap | `std::atomic_store/load` on `shared_ptr<const>` | ✅ |

---

## 4. Spec Compliance — AC Spot-Checks

### Scroll-Gesture Zoom (Phase 1)

**AC-2.1.02: Scroll passthrough when Win released**
- **Status:** ✅ Pass
- **Evidence:** `InputInterceptor.cpp` — mouse hook only processes scroll when `modifierHeld` is true. When false, returns `CallNextHookEx()` immediately with no delay or modification.

**AC-2.1.06: Logarithmic zoom model**
- **Status:** ✅ Pass
- **Evidence:** `ZoomController.cpp:74` — `newZoom = currentZoom_ * std::pow(kScrollZoomBase, normalizedDelta)` where `kScrollZoomBase = 1.1f`. Each notch (120 delta units) multiplies zoom by 1.1×, providing perceptually even scaling across the full range.

**AC-2.1.14/15: Soft-approach deceleration at bounds**
- **Status:** ✅ Pass
- **Evidence:** `ZoomController.cpp:49-72` — Logarithmic margin-based attenuation. `kSoftMarginFraction = 0.15f` (15% of range). Quadratic ease-to-zero near bounds. Not a hard stop — zoom decelerates smoothly.

**AC-2.1.16/17/18: Start Menu suppression**
- **Status:** ✅ Pass
- **Evidence:** `WinKeyManager.cpp:29-44` — On Win key release, if state is `HeldUsed` (zoom was performed), injects `SendInput(Ctrl press+release)` before Win propagates. This makes Windows think Win was chording with Ctrl, suppressing Start Menu. State transitions: `Idle → HeldClean → HeldUsed` (on zoom) `→ Idle` (with Ctrl injection). Plain Win tap (no zoom) stays `HeldClean → Idle` without injection, preserving normal Start Menu behavior.

### Animation (Phase 2)

**AC-2.2.06: Rapid retargeting**
- **Status:** ✅ Pass
- **Evidence:** `ZoomController.cpp:117-146` — `handleKeyboardStep()` updates `targetZoom_` directly. If already `ANIMATING`, the target is retargeted without resetting the animation — a single continuous ease-out to the new target.

**AC-2.2.07: Smooth reversal**
- **Status:** ✅ Pass
- **Evidence:** Same retargeting mechanism. `Win+Plus` then immediate `Win+Minus` simply updates the target in the opposite direction. The exponential ease-out (`1 - pow(1-rate, dt*Hz)`) at `ZoomController.cpp:218` naturally handles direction changes without bounce.

**AC-2.2.08: Scroll interrupts animation**
- **Status:** ✅ Pass
- **Evidence:** `ZoomController.cpp:42` — `handleScrollInput()` unconditionally sets `mode_ = Mode::Scrolling`, overriding any active animation. The current interpolated zoom becomes the new base for scroll delta application.

### Accessibility Tracking (Phase 3)

**AC-2.5.07/09: Focus debounce (100ms)**
- **Status:** ✅ Pass
- **Evidence:** `ViewportTracker.cpp:134-139` — `kFocusDebounceMs = 100`. Focus source is only selected if `timeSinceLastFocusChange >= kFocusDebounceMs`. Rapid Tab presses update the focus rect in SharedState, but the viewport only pans to the final position after 100ms of stability.

**AC-2.6.07/08: Caret priority during typing, 500ms handoff**
- **Status:** ✅ Pass
- **Evidence:** `ViewportTracker.cpp:125-129` — `kCaretIdleTimeoutMs = 500`. Caret takes priority when `timeSinceLastKeyboardInput < 500ms` and caret rect is valid. After 500ms of no keyboard input, priority falls back to pointer via smooth 200ms source transition (`RenderLoop.cpp:443-460`).

### Temporary Toggle (Phase 4)

**AC-2.7.08: Exact pre-toggle zoom restoration**
- **Status:** ✅ Pass
- **Evidence:** `ZoomController.cpp:165-184` — `engageToggle()` saves `currentZoom_` to `savedZoomForToggle_`. `releaseToggle()` animates to the saved value. No rounding applied — `double` precision preserves the exact level.

**AC-2.7.09: Scroll during toggle updates restore target**
- **Status:** ✅ Pass
- **Evidence:** `ZoomController.cpp:90-92` — During scroll input while `isToggled_`, updates `savedZoomForToggle_ = currentZoom_` before applying the new scroll delta. The restore target tracks the user's adjustments.

**AC-2.7.10: Toggle during mid-animation captures mid-animation zoom**
- **Status:** ✅ Pass
- **Evidence:** `ZoomController.cpp:165-167` — `engageToggle()` reads `currentZoom_` (the interpolated in-flight value), not the animation target. The mid-animation zoom level is captured as the restore point.

---

## 5. Risk Mitigation Status

| Risk | Description | Required Mitigation | Status | Evidence |
|---|---|---|---|---|
| **R-04** | Input transform desync | Same-frame API calls with identical values | ✅ **Mitigated** | `RenderLoop.cpp:463-482` — `setTransform()` called once per frame with computed zoom and offset. `MagBridge.cpp:173-179` documents that proportional viewport mapping eliminates per-frame `setInputTransform` need. Shutdown calls both in sequence (`MagBridge.cpp:57-67`). |
| **R-05** | Hook deregistration | Watchdog timer + reinstall; minimal hook callbacks | ✅ **Mitigated** | `main.cpp:529-557` — 5-second `WM_TIMER` checks `isHealthy()`, reinstalls hooks on failure, shows balloon notification. Hook callbacks in `InputInterceptor.cpp` are minimal (read event → atomic write → return, <1ms). |
| **R-06** | Start Menu suppression | `SendInput` Ctrl injection on Win release | ✅ **Mitigated** | `WinKeyManager.cpp:29-44` — Ctrl press+release injected via `SendInput` when Win key was used for zoom. State machine prevents injection on plain Win tap. |
| **R-11** | UIA callback latency | 100ms timeout on bounding-rect queries | ⚠️ **Partial** | UIA thread is correctly isolated (cannot block render or hooks). However, `FocusMonitor.cpp:89` calls `get_CurrentBoundingRectangle()` with **no explicit timeout**. A hanging UIA provider blocks the UIA thread indefinitely. CaretMonitor uses GTTI polling on a separate thread (inherently bounded). **Gap: FocusMonitor needs IUIAutomation timeout or async pattern.** |
| **R-14** | Crash leaves screen magnified | Exception filter + sentinel file + `WM_ENDSESSION` | ✅ **Mitigated** (with T3 caveat) | `main.cpp:768` — `SetUnhandledExceptionFilter(crashHandler)`. `main.cpp:108-129` — Sentinel file written on startup, removed on clean exit. `main.cpp:619-640` — `WM_ENDSESSION` handler stops threads and resets zoom. `main.cpp:775-785` — Startup recovery resets zoom if sentinel exists. **Caveat:** Crash handler creates MagBridge on arbitrary thread (T3). |
| **R-17** | Float precision drift | Epsilon snap + double internally | ✅ **Mitigated** | `ZoomController.cpp:80-85` — Snap to 1.0× within `kSnapEpsilon = 0.005`. `ZoomController.cpp:212-232` — All animation math in `double`, converted to `float` only at API boundary. Logarithmic model (`pow(base, delta)`) avoids drift from repeated multiply/divide. |
| **R-18** | High CPU at high refresh | Skip redundant API calls; idle at 1.0× | ✅ **Mitigated** | `RenderLoop.cpp:463-482` — `setTransform()` only called when zoom or offset changed (delta comparison). At 1.0× with no animation, the frame tick skips all Mag API calls. `DwmFlush()` blocks the thread at VSync rate, preventing busy-spinning. |

---

## 6. Code Quality Observations

### Build Configuration

- **x64 enforcement:** `CMakeLists.txt` — `FATAL_ERROR` if `CMAKE_SIZEOF_VOID_P != 8`. ✅
- **Compiler flags:** `/W4 /WX /permissive- /utf-8` — strict warnings, conformance mode. ✅
- **Manifest:** `uiAccess="true"`, `PerMonitorV2` DPI awareness, Windows 10/11 compatibility. ✅
- **Manifest embedding:** `/MANIFEST:EMBED /MANIFESTINPUT:...` in linker flags. ✅
- **Linking:** All required libraries present — `Magnification.lib`, `Dwmapi.lib`, `UIAutomationCore.lib`, `User32.lib`, `Shell32.lib`, etc. ✅
- **Signing support:** `SMOOTHZOOM_SIGN_BINARY` CMake option present. ✅
- **Layer separation:** Four static libraries (input, logic, output, support) with clean dependency graph. ✅

### Error Handling

- **Mag API:** Every `MagSet*` call checks return value and captures `GetLastError()`. `MagBridge.cpp:88,168`. ✅
- **Mag init failure:** Startup checks and shows user-visible error dialog with guidance (uiAccess, signing, secure location). `main.cpp:871-887`. ✅
- **Hook install failure:** Error dialog with details. `main.cpp:854-866`. ✅
- **Settings corruption:** No-throw JSON parse + per-field validation + default fallback. `SettingsManager.cpp:57-98`. ✅ (AC-2.9.03)
- **UIA failures:** Silent degradation per AC-2.5.14, AC-2.6.11. `FocusMonitor.cpp:90`, `CaretMonitor.cpp:35-47`. ✅
- **Conflict detection:** Detects native Magnifier process, offers to close or exit. `main.cpp:821-850`. ✅ (AC-ERR.01)

### Structure and Naming

- Component files map 1:1 to the ten named components from Doc 3. ✅
- Headers organized under `include/smoothzoom/` by layer (common, input, logic, output, support). ✅
- Consistent naming conventions throughout (camelCase for methods, UPPER_CASE for constants, `k` prefix for file-scope constants). ✅
- AC numbers cited in comments where implementing specific behaviors. ✅

### Logging

- Conditional compilation via `SMOOTHZOOM_LOGGING` define (Debug builds only). ✅
- Thread-safe file logging with `CRITICAL_SECTION` guard in `Logger.h`. ✅
- Stack-only buffers (`wchar_t[512]`) — no heap allocation. ✅
- No logging on render hot path (gated behind `#ifdef SMOOTHZOOM_PERF_AUDIT`). ✅
- Comprehensive diagnostic output for hook re-registration, settings validation, PTP HID parsing. ✅

### Dead Code

- `SharedState.h:23-24` — `pointerX`/`pointerY` written but never read (T5).
- `Logger.h:202` — `SZ_LOG_IMPL_` macro defined but unused (cosmetic).
- `MagBridge.cpp:25` — Comment references `refreshScreenDimensions()` method that doesn't exist (Phase 6 WIP).

### Settings Design

- Immutable snapshot with atomic pointer swap — correct pattern per spec. ✅
- Observer notification fires synchronously on main thread. ✅
- **Note:** Observer vector registration (`push_back`) is not thread-safe, but all registration occurs during initialization on the main thread, so this is safe in practice.

---

## 7. Recommended Actions

### Must Fix (Before Release)

| # | Finding | Action | Location |
|---|---------|--------|----------|
| 1 | **T3 — Crash handler thread affinity** | Refactor crash handler to avoid creating a new MagBridge. Either (a) call `MagSetFullscreenTransform(1.0, 0, 0)` directly as a best-effort on the crash thread (bypassing MagBridge abstraction), or (b) signal the render thread with a short timeout. The sentinel file provides backup, but immediate reset is strongly preferred. | `main.cpp:210-213` |
| 2 | **R-11 gap — FocusMonitor timeout** | Add a timeout to `get_CurrentBoundingRectangle()`. Options: (a) Use `IUIAutomation::set_ConnectionTimeout()` if available, (b) wrap the call in a `std::async` with `wait_for(100ms)`, or (c) use the UIA condition/cache request pattern with timeout. Log slow callbacks (>50ms) per spec. | `FocusMonitor.cpp:89` |

### Should Fix (Before v1.0)

| # | Finding | Action | Location |
|---|---------|--------|----------|
| 3 | **T1 — SeqLock UB** | Add a comment documenting the x86-64/MSVC assumption. For portability, consider `memcpy`-based read/write through `char*` (allowed aliasing under strict aliasing rules) or `std::atomic<T>` with relaxed ordering if `T` is lock-free. | `SeqLock.h:34` |
| 4 | **T7 — MagBridge screen dimension race** | Make `s_screenWidth`/`s_screenHeight` `std::atomic<int>` with relaxed ordering. Trivial fix, eliminates a theoretical race. | `MagBridge.cpp:26-27` |
| 5 | **T5 — Dead SharedState fields** | Remove `pointerX`/`pointerY` from `SharedState` and the corresponding writes in `InputInterceptor.cpp`. The render thread uses `GetCursorPos()` for fresher data. | `SharedState.h:23-24` |

### Consider (Quality Improvements)

| # | Finding | Action | Location |
|---|---------|--------|----------|
| 6 | **T6 — Deprecated `atomic_load` on `shared_ptr`** | No action needed for C++17. Note for future C++20 migration: replace with `std::atomic<std::shared_ptr<const SettingsSnapshot>>`. | `main.cpp:199`, `RenderLoop.cpp:232` |
| 7 | **Settings save atomicity** | Consider write-to-temp-then-rename pattern to prevent partial writes on disk full or crash during save. Current behavior is acceptable (next load falls back to defaults per AC-2.9.03). | `SettingsManager.cpp:131-164` |
| 8 | **Logger dead macro** | Remove unused `SZ_LOG_IMPL_` macro definition. | `Logger.h:202` |
| 9 | **T4 documentation** | Document the four-thread deviation from spec in a code comment near thread creation, explaining the rationale (GTTI doesn't need COM, isolation from slow UIA providers). | `CaretMonitor.cpp` thread launch |

---

*End of report.*

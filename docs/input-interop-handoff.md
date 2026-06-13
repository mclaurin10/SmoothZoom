# SmoothZoom — Input Interoperability: Session Handoff

**Purpose:** continue the input-interoperability work after a context clear. Read this top-to-bottom; it is self-contained.

**Date of handoff:** 2026-06-06
**Branch:** `master`

---

## 1. Goal (one line)

Make zoom/pan/scroll behave consistently across high-precision OEM trackpads (Synaptics, Elan, …), Apple Magic Trackpad, notched mice, free-spin mice, and trackballs.

## 2. Where we are

A **two-phase review + plan** was produced and **approved**, then the **P0 backend slice was implemented and verified** (full build clean under `/W4 /WX`; unit tests green). Remaining work is tracked as follow-up items (§7).

- **Approved plan (full inventory + prioritized plan):**
  `C:\Users\duncan\.claude\plans\you-are-reviewing-smoothzoom-zazzy-frost.md`
  Read this for the complete Phase-1 inventory (every input API, delta semantics, DPI math, pipeline) and Phase-2 rationale. This handoff summarizes; the plan is the authority.

## 3. Scope decisions already made (do not re-litigate)

| Topic | Decision |
|-------|----------|
| Pinch-to-zoom | **Investigate only** — feasibility spike, no shipping commitment (Doc 1 §3.7 still lists it out of scope). |
| Pan | **Auto-tracking only** — no new user-driven pan gesture; just normalize the existing pointer/focus/caret follow. |
| Capture backend | **Windows Magnification API as found on disk.** There is **no** Desktop Duplication (DXGI/D3D11) code here despite the "DDA fork" framing. Keep the input layer backend-independent. |
| Momentum/inertia | **Make it configurable** (recommended default: on). Setting field already added; gating logic deferred to B3. |

## 4. Key facts about the codebase (verified)

- **Three parallel scroll-ingest paths** feed one `std::atomic<int32_t> scrollAccumulator` (`SharedState.h:23`), drained once/frame by `RenderLoop::frameTick` via `exchange(0)`:
  1. LL mouse hook `WM_MOUSEWHEEL`/`WM_MOUSEHWHEEL` — `src/input/InputInterceptor.cpp` `mouseHookProc`
  2. Raw Input mouse `WM_INPUT`/`RIM_TYPEMOUSE` — `src/app/main.cpp` `msgWndProc`
  3. Precision-Touchpad HID two-finger — `src/app/main.cpp` `handlePtpHidReport` / `initPtpDevice`
- Zoom math: `ZoomController::applyScrollDelta` normalizes `delta / 120` then `pow(1.1, normalizedDelta)` (logarithmic). Scroll zoom is **direct** (not animated); only keyboard/toggle ease-out.
- DPI/coords are **already correct**: PerMonitorV2 manifest, physical-pixel virtual-desktop space, float division, origin-aware clamping. Not a problem area.
- `WM_POINTER`, `WM_GESTURE`, Direct Manipulation, `WM_TOUCH` are **not** used.

## 5. What P0 changed (this session) — all committed to the working tree, not yet `git commit`ed

New files:
- `include/smoothzoom/input/ScrollNormalizer.h` — pure, header-only, **no Win32**. Converts device input → device-independent "wheel-equivalent units" (120 = one notch). Holds the PTP device-range normalization (A1) and identity mouse entry points (for the future consolidation). Tunable constant `kPtpSurfaceFractionPerNotch = 0.02f` (≈2% of pad per notch) and fallback `kPtpFallbackUnitsPerNotch = 25.0f`.
- `tests/unit/test_ScrollNormalizer.cpp` — 9 cases (identity, device-range independence, sign, sub-notch, fallback).

Edited:
- `src/app/main.cpp` (A1): `PtpDeviceInfo` gained `logicalRangeY`; `initPtpDevice` captures the Y-axis logical range (Generic-Desktop usage 0x31); `handlePtpHidReport` now uses `ScrollNormalizer::ptpDeltaToWheelEquiv` with a **float** remainder `s_ptpWheelRemainder` (continuous sub-notch scroll), replacing the hardcoded `kPtpUnitsPerNotch=25` + integer-notch logic.
- `include/smoothzoom/logic/ZoomController.h` + `src/logic/ZoomController.cpp` (A3): `applySettings(...)` gained `float scrollSensitivity = 1.0f`; `scrollSensitivity_` member; `applyScrollDelta` multiplies the normalized delta by it (default 1.0 = unchanged).
- `include/smoothzoom/support/SettingsManager.h` + `src/support/SettingsManager.cpp` (A3): added `scrollSensitivity` (0.1–5.0, default 1.0) and `momentumZoom` (default true) to `SettingsSnapshot`, with load/validate/save.
- `src/logic/RenderLoop.cpp`: passes `snap->scrollSensitivity` into `applySettings`.
- `CMakeLists.txt`: added `tests/unit/test_ScrollNormalizer.cpp` to the `smoothzoom_tests` target.
- `tests/unit/test_SettingsManager.cpp`, `tests/unit/test_ZoomController.cpp`: added interop coverage.

Net behavior at defaults is unchanged for a standard notched mouse; PTP scroll is now device-normalized and continuous; sensitivity is tunable (via config.json only — see #5 in §7).

## 6. Build & test (IMPORTANT — toolchain is not on PATH)

`cmake`/`ctest` are **not** on PATH and the existing `build/` dir is **stale** (older CMake). Use the Visual Studio 2026 CMake directly and a fresh build dir:

```powershell
$cmake = "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
# Configure (reuse already-downloaded Catch2 to avoid network):
& $cmake -S "C:\Dev\SmoothZoom" -B "C:\Dev\SmoothZoom\build2" -G "Visual Studio 18 2026" -A x64 `
    -D "FETCHCONTENT_SOURCE_DIR_CATCH2=C:/Dev/SmoothZoom/build/_deps/catch2-src"
# Build everything (compiles main.cpp under /W4 /WX) + tests:
& $cmake --build "C:\Dev\SmoothZoom\build2" --config Debug
# Run unit tests:
& "C:\Dev\SmoothZoom\build2\Debug\smoothzoom_tests.exe"
```

- `build2/` is a scratch dir (gitignore only covers `build/`); delete it before committing, or add to `.gitignore`.
- **Last verified result:** `All tests passed (371 assertions in 131 test cases)`; full project links clean.
- Note: `scripts\build.bat` says generator `Visual Studio 17 2022`, which won't match this machine (has VS 2026 / cmake 4.2). Pre-existing; not changed.

## 7. Remaining work (priority order) — see tasks #5–#8

- **#5 A3-UI** — add a scroll-sensitivity slider + momentum checkbox to the settings window (`src/support/TrayUI.cpp`). Today `scrollSensitivity`/`momentumZoom` are editable only by hand-editing `config.json`. This is the smallest, highest-value next step to make A3 user-facing.
- **#6 A2-consolidate** — move the Raw Input/PTP handlers out of `src/app/main.cpp` into the Input layer (e.g. a `ScrollIngest` component or `InputInterceptor`) so all three paths share one funnel, dedup, and modifier check; route mouse paths through `ScrollNormalizer::mouseWheelToWheelEquiv`. Behavior-preserving refactor; respect hot-path invariants.
- **#7 P1 robustness (B1/B2/B3):**
  - B1 — per-device dedup via `RAWINPUTHEADER.hDevice`, replacing the coarse 50 ms global `lastLLHookScrollTime` window (can drop high-frequency events).
  - B2 — high-res/free-spin per-frame rate clamp / gentle acceleration in the normalizer (don't change notched feel).
  - B3 — implement `momentumZoom` gating (field already persists): PTP contact lift-off boundary + a wheel-path decay heuristic.
- **#8 P2:** C1 pinch feasibility spike (reuse `handlePtpHidReport` contact data; no production code); C2 handle `WM_DPICHANGED` in `msgWndProc`.

Several P1 items need **real-hardware tuning** (see plan §"What cannot be verified from code alone").

## 8. Hard constraints the next session MUST respect

- **No `#include <Magnification.h>` outside `MagBridge.cpp`** (R-01 isolation) — exception is the crash handler in `main.cpp`.
- **Hot-path invariants in `frameTick()`** (`.claude/rules/render-loop.md`): no heap alloc, no mutex, no I/O, no blocking but `DwmFlush()`. The scroll accumulator is consumed with `exchange(0)`.
- **Hook callbacks must be minimal** (`.claude/rules/input-hooks.md`, R-05): read event, atomic write, return. The PTP HID parsing runs in the **main-thread message pump** (`WM_INPUT`), not in a hook callback and not on the render hot path — float math there is fine.
- Build is **x64 only**, MSVC, C++17, `/W4 /WX` (warnings are errors). `inline constexpr` in headers is fine under C++17 (clangd may flag it — that's editor noise; trust the MSVC build).
- clangd in the editor lacks the CMake include paths and will report false "file not found"/"undeclared identifier" errors. **Ignore them; verify via the CMake build.**

## 9. Open questions / assumptions to revisit

- `kPtpSurfaceFractionPerNotch = 0.02` is a **reasoned default, not hardware-verified** — tune against real Synaptics/Elan pads; users can compensate via `scrollSensitivity`.
- A1 uses **logical-range fraction** (always available) rather than physical-mm; revisit if absolute finger-travel parity is preferred.
- Separate mouse vs touchpad sensitivity sliders? (Currently one shared `scrollSensitivity`.)
- `momentumZoom` default — recommend **on** (macOS-like). Confirm with user.
- Whether to ever adopt the pointer-input stack (`EnableMouseInPointer` / `WM_POINTERSCROLL`) for native high-res/gesture data — larger change, currently out.

## 10. Pointers

- Plan: `C:\Users\duncan\.claude\plans\you-are-reviewing-smoothzoom-zazzy-frost.md`
- Project rules: `.claude/rules/{render-loop,input-hooks,mag-bridge,project-constraints}.md`
- Behavior spec: `docs/02_Behavior_Specification.md` (AC-2.1.* scroll, AC-2.2.* smoothness, AC-2.4.* tracking)
- Risks: `docs/05_Technical_Risks_and_Mitigations.md` (R-05 hooks, R-07 modifier conflicts, R-08 touchpad formats)

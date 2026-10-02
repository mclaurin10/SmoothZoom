# SmoothZoom — Laptop-Baseline Audit (read-only)

**Date:** 2026-10-01 · **Branch:** `perf/laptop-baseline` (from `master` @ `4d61274`) · **Machine:** TEMPLAPTOP (see `hardware/TEMPLAPTOP.md`)

Scope: a read-only audit to support the build → deploy → bench loop. No code was changed for this
document. Citations are `file:line` against `4d61274`.

**Verdict: no blocking issue.** The toolchain is present and builds cleanly. Docs and code agree on
everything the bench depends on: pacing, the wheel hand-off and the character-leak fix. The
disagreements found are listed in §6. None of them blocks a baseline.

---

## 1. Build system and existing scripts

| Item | Finding |
|---|---|
| Build | CMake ≥ 3.20, C++17, `/W4 /WX /permissive- /sdl /guard:cf` (`CMakeLists.txt:20-23`). Static libs per layer (`smoothzoom_output/input/logic/support`, `CMakeLists.txt:89-166`) linked into `SmoothZoom` (WIN32 exe, `:171-196`). The manifest is embedded via `/MANIFEST:EMBED /MANIFESTINPUT:res/SmoothZoom.manifest` (`:199-203`). |
| Options | `SMOOTHZOOM_BUILD_TESTS` (ON), `SMOOTHZOOM_SIGN_BINARY` (OFF, **unused** — no CMake logic reads it), `SMOOTHZOOM_LOGGING` (ON), `SMOOTHZOOM_INPUT_DIAG` (OFF; per-event `OutputDebugStringW` *inside* the hook, an R-05 hazard), `SMOOTHZOOM_PERF_AUDIT` (OFF; 600-frame `frameTick` cost stats via ODS) (`CMakeLists.txt:28-68`). |
| Tests | Catch2 v3.5.2 via FetchContent (needs network on first configure), pure-logic sources compiled directly into `smoothzoom_tests` (`CMakeLists.txt:230-266`). **Verified on this laptop:** `100% tests passed` (Release). |
| `scripts/build.bat` | Hard-codes `-G "Visual Studio 17 2022"` (`scripts/build.bat:15`). **This fails on this laptop**: only *VS 2026 Build Tools* (v18, MSVC 14.51.36231) is installed, so the 2022 generator has no instance. The same applies to `scripts/deploy.ps1:41`. The new `tools/` scripts detect the generator via `vswhere` instead. |
| `scripts/deploy.ps1` → `deploy-machinestore.ps1` | Build, then sign and install in an **elevated** session (`Assert-Elevated`, `scripts/_signing-common.ps1:21`; `deploy-machinestore.ps1:37-43`). It stops running instances with `Stop-Process -Force` (`deploy-machinestore.ps1:51-55`). |
| Repo-root `deploy.ps1` | Deprecated wrapper that forwards to `scripts/deploy.ps1` (`deploy.ps1:32-38`). |
| `cmake`/`signtool` | Neither is on PATH. CMake 4.3.1 + Ninja ship inside Build Tools (`…\Common7\IDE\CommonExtensions\Microsoft\CMake\`). `signtool` is at `C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\signtool.exe`. |

## 2. Signing, deploy and UIAccess flow

- **Manifest:** `asInvoker` + `uiAccess="true"` + PerMonitorV2 (`res/SmoothZoom.manifest`). This matches Doc 3 §5.3 and Hard Constraints #1 and #3.
- **Signing (existing):** a self-signed `CN=SmoothZoom Dev` cert lives in **LocalMachine\My**. It is trusted in **LocalMachine\Root** (`_signing-common.ps1:40-73`) and used via `signtool sign /sm /sha1 <thumb> /fd SHA256` (`:76-83`). Every step requires elevation. The machine store exists for split-account machines where the login user is not an admin (`_signing-common.ps1:4-9`; `docs/hardware-accommodation-handoff.md` §6.5).
- **Install:** the scripts copy to `C:\Program Files\SmoothZoom\` (`deploy-machinestore.ps1:33,103-108`). `install-secure.ps1` refuses anything whose Authenticode status is not `Valid` (`install-secure.ps1:36-46`).
- **Launch:** the UIAccess launch is **brokered** by AppInfo. `CreateProcess` fails with `ERROR_ELEVATION_REQUIRED`, and `ShellExecute`/`Start-Process` works but **does not carry the caller's environment** (`hardware-accommodation-handoff.md` §6.2). Command-line arguments *do* pass through, so the dev tools are driven by arguments, not environment variables.
- **R-12 failure mode:** the app treats `MagInitialize` failure as fatal and shows a modal (`main.cpp:1349-1367`). An unsigned or untrusted binary gets past `MagInitialize`, but `MagSetFullscreenTransform` returns `FALSE` silently. The only symptom is the first-failure log line in `RenderLoop.cpp:598-599`. The bench therefore records `ok` per update and fails loudly if any update failed.
- **This laptop:** the user `duncan` is a split-token admin (medium IL, Administrators deny-only). No SmoothZoom cert exists in any store, and `C:\Program Files\SmoothZoom\` does not exist. Every existing deploy path needs a UAC prompt per deploy, which stalls unattended runs. `tools/dev-setup.ps1` addresses this (see `tools/README.md` and the tradeoffs section of the session report).

## 3. How the render loop paces transform updates

```
threadMain (RenderLoop.cpp:172-235), render thread:
  MagBridge.initialize()                        :179   (Mag* thread affinity)
  conflict read-back MagGetFullscreenTransform  :193-202
  while !shutdown:
      frameTick()                               :215
      PeekMessage/DispatchMessage pump          :220-225  (required, CLAUDE.md Known Issue #1)
      DwmFlush()                                :227   (the only pacing primitive)
```

`frameTick()` (`RenderLoop.cpp:251-654`) works as follows.
1. Settings version check (`:260-289`).
2. `scrollAccumulator.exchange(0)` (`:298`).
3. Drain the command queue (`:301-335`).
4. Apply scroll **directly** via `ZoomController::applyScrollDelta` (`:338-343`). No interpolation in scroll mode (AC-2.2.03).
5. Compute dt from QPC, clamped to 0..100 ms (`:346-357`). `ZoomController::tick(dt)` (`:359`) only animates in `Animating` mode, using a frame-rate-independent ease-out `alpha = 1-(1-rate)^(dt·60)` (`ZoomController.cpp:214-240`).
6. `endScroll()` on any frame without new scroll input (`:365-366`). The mode drops Scrolling → Idle after **one** empty frame.
7. Idle short-circuit at exactly 1.0× (`:384-390`).
8. `GetCursorPos`, per-monitor deadzone of `3·monH/1080` px (`:401-458`), source arbitration (`:497-499`), offset by source (`:502-544`), 200 ms source-transition blend (`:548-588`).
9. **Only if zoom or offset changed** (`:591`), call `MagBridge::setTransform` (`:595`). `MagBridge::setTransform` **truncates the float offsets to `int`** before `MagSetFullscreenTransform` (`MagBridge.cpp:87-89`).

Pacing consequences relevant to the bench:
- **At most one transform per `DwmFlush`.** The bench verifies this rather than assuming it. If `DwmFlush` returns early (for example when DWM has nothing queued), two updates can land in one refresh.
- **Phase:** the update is issued immediately after `DwmFlush` returns, i.e. *early* in the refresh interval. R-03 mitigation 2 asks for the call "as late as possible … just before DwmFlush". Loop order puts it before `DwmFlush`, but in time it sits near the start of the frame. Expected input → visible latency is therefore ≤ 1 period to the update plus ≤ 1 period to scan-out. The bench measures the phase relative to vblank via `DwmGetCompositionTimingInfo`.
- **Input quantisation:** the scroll for a frame is whatever accumulated since the last `exchange`. A touchpad at ~125 Hz into a 59.89 Hz loop delivers 2 or 3 events per frame, unevenly. That is a likely source of judder that the trace metrics should expose.
- **Sub-pixel loss:** offset truncation means the zoom anchor (the desktop point under the pointer, AC-2.1.09–11) moves by up to `zoom` physical px per update, i.e. up to ~10 px at 10×. This is not documented in Doc 3 §3.4/§3.8 and is a prime suspect for zoom shimmer. The bench reports it as "anchor error".

## 4. Where InputInterceptor hands wheel events to ZoomController

There is no direct call. The hand-off is a single atomic:

| Step | Location |
|---|---|
| `WH_MOUSE_LL` installed on the main thread | `InputInterceptor.cpp:426` |
| `WM_MOUSEWHEEL`: modifier test via `isConfiguredModifierHeld()` (Win: `GetAsyncKeyState` cross-check plus WinKeyManager state; others: the hook-tracked flag OR `GetAsyncKeyState`) | `InputInterceptor.cpp:88-104, 139` |
| Delta = `(int16_t)HIWORD(mouseData)` | `:144` |
| Stamp `lastLLHookScrollTime` (Raw Input/PTP dedup, GetTickCount64 domain) | `:147-148` |
| **`scrollAccumulator.fetch_add(delta)`**, the hand-off | `:151` |
| `markUsedForZoom()` if the modifier is Win (Start-menu suppression) | `:154-155` |
| `return 1` (consume, AC-2.1.02) | `:158` |
| `WM_MOUSEHWHEEL`: same, but only when **Shift** is the configured modifier, with the delta **negated** | `:163-208` |
| Render thread `exchange(0)` → `applyScrollDelta(delta)` (`reverseScrollDirection` applied first) | `RenderLoop.cpp:298, 338-343` |
| Log model `zoom *= 1.1^(delta/120 · scrollSensitivity)`, soft bounds on the outer 15% of log range, snap to 1.0/max within 0.005 | `ZoomController.cpp:37-100` |

Two more producers write the same accumulator and **bypass the hook entirely**. Both are in `main.cpp`, which is known debt (Doc 3 §3.1 note):
- Raw Input mouse fallback (`main.cpp:1078-1109`), deduped against the hook with a 50 ms window.
- Precision-touchpad HID two-finger path (`main.cpp:609-785`). Device units are normalised by `ScrollNormalizer` (`kPtpSurfaceFractionPerNotch = 0.08`, `ScrollNormalizer.h:54`) with a float remainder carried between reports. **On this ELAN touchpad, scrolling in Direct-Manipulation apps (Edge, Explorer, Settings) probably arrives *only* on this path** (`hardware-accommodation-handoff.md` §3). A hook-only recorder would miss it, so `--record` also logs these two paths (kinds `RAW`/`PTP`).

## 5. Keyboard-hook character-leak fix: **present**

`InputInterceptor.cpp:402-412`: `if ((isDown || isUp) && isConfiguredModifierHeld())` consumes `VK_OEM_PLUS`, `VK_ADD`, `VK_OEM_MINUS` and `VK_SUBTRACT` (`return 1`). Every other key falls through to `CallNextHookEx` (`:415`). It matches `.claude/rules/input-hooks.md` ("Keyboard Hook") and Doc 3 §3.1 step 6. The zoom command itself is posted on key-down only (`:327-349`). Injected (`LLKHF_INJECTED`) events are consumed deliberately, for peripheral macros (`:399-401`).

Observed nuance (reported only, not changed): if the modifier is released *before* the zoom key, that key's **key-up** passes through. That is harmless, because a lone key-up produces no character. Not tested on hardware this session; per the brief this is status-only.

## 6. Doc ↔ code disagreements (none blocking)

| # | Docs say | Code does | Load-bearing for the bench? |
|---|---|---|---|
| D1 | Doc 3 §2.1 and §3.8: `MagInitialize` on the **main** thread | Render thread (`RenderLoop.cpp:179`). `CLAUDE.md` and `rules/mag-bridge.md` also say render thread | No. Code follows `CLAUDE.md`, and Doc 3 is internally stale here. |
| D2 | `CLAUDE.md` and R-17: "`double` internally for zoom math" | `currentZoom_`/`targetZoom_` are **`float`** (`ZoomController.h:67-68`). `double` is used only inside `tick()` (`ZoomController.cpp:226-238`) | **Indirectly.** Float state limits zoom round-trip precision, which the bench measures. A candidate for a later session, not a blocker. |
| D3 | Doc 3 §3.3: additive keyboard step; soft zone = 5% of linear range | Multiplicative step (`ZoomController.cpp:106`); soft zone = 15% of **log** range, quadratic (`:35, 60-75`) | No (keyboard path and bounds; the wheel traces only touch the soft zone at the ends). |
| D4 | Doc 3 §3.1 and `rules/input-hooks.md`: the mouse hook writes `WM_MOUSEMOVE` positions | Not implemented; RenderLoop uses `GetCursorPos` (`RenderLoop.cpp:401-402`), as the constraints require | No. |
| D5 | Doc 3 §3.7 pseudocode: apply scroll, *then* drain commands | Drains commands first, applies scroll last (`RenderLoop.cpp:298-343`) | No (wheel-only traces). |
| D6 | Doc 3 §3.4/§3.8 imply exact proportional mapping | Offsets truncated to `int` at the API (`MagBridge.cpp:87-89`) | **Measured, not blocking.** This is the "anchor error" metric. |
| D7 | Brief: "three-thread model" | Four threads (Main, Render, UIA, Caret), per `CLAUDE.md` and Doc 3 §2 | No. The bench adds one dev-only thread (see Doc 3 §7). |
| D8 | Doc 3 §5.1: "VS 2022+" | Scripts pin the VS 2022 generator (`scripts/build.bat:15`, `scripts/deploy.ps1:41`) | No. The new tools auto-detect the generator. |

## 7. Implications carried into the bench design

- The bench drives the real path: replayed wheel events go through the hook's own decision function into `scrollAccumulator`, and the render thread consumes them exactly as in production. The bench does not call ZoomController directly.
- Telemetry is captured on the render thread into a **pre-allocated** ring with no I/O, and is only compiled when `SMOOTHZOOM_DEV_TOOLS=ON` (render-loop invariants 1–3).
- Anchor error is computed against the float proportional offset from the **raw** cursor (not the deadzone-committed one). This captures both truncation (D6) and deadzone lag.

# SmoothZoom — Manual Test Checklist v2

Phase 6 polish: bug-fix verification, remaining ACs, core regression spot-check.

> **Verification log — 2026-06-18 (dev hardware, signed Release build installed to
> `C:\Program Files\SmoothZoom`, zoom visually confirmed by user):**
> - Color inversion Ctrl+Alt+I toggles + persists to `config.json` and survives
>   restart (bidirectional False→True→False round-trip verified live). ✅
> - Config `schemaVersion` key added; a legacy (unversioned) config migrates to
>   version 1 on first save. ✅
> - Single-instance guard: 2nd launch shows the "already running" dialog (`#32770`)
>   and the first instance is undisturbed. ✅
> - Performance (E6.12, CPU only): zoomed+stationary **0.00%** (<2%), zoomed+moving
>   **0.06%** (<5%), not-zoomed idle 0.03%. PROVISIONAL — dev hardware, not the
>   Intel UHD 620 reference box; GPU / memory / latency not yet measured. ✅ (CPU)
> - Visual ACs (confirmed via screenshot of the magnified output): scroll-gesture
>   zoom engages and is **centered on the pointer** (AC-2.1.01/09); **viewport
>   follows the pointer** when it moves (AC-2.4 / E1.1); magnification is smooth/
>   clean (AC-2.3.x); **Shift+Esc resets to 1.0×** (AC-2.8.08). (Modifier is Shift
>   in this machine's config; reset = modifier+Esc.) ✅
> - Working verification methodology: drive input via SendInput/keybd_event
>   injection (reaches the LL hooks) + observe via computer-use screenshots (DO
>   capture the DWM Magnification overlay). computer-use's own scroll does NOT
>   reach the hook; a medium-IL shell cannot kill `uiAccess` procs or inject over a
>   higher-IL foreground window (elevation needed for those).
> - Still needs an interactive/elevated pass: Win+L lock/unlock (secure desktop),
>   sleep/resume (S1), DPI-change (S2), multi-monitor (needs 2nd display), UAC
>   (E6.10), and the full AC-2.4–2.10 sweep.

## 1. Bug Fix Verification

### WS1: Keyboard Shortcuts Respect Configurable Modifier

- [ ] **Default (Win):** Win+Plus zooms in, Win+Minus zooms out, Win+Esc resets
- [ ] Change modifier to **Ctrl** in settings:
  - [ ] Ctrl+Plus zooms in
  - [ ] Ctrl+Minus zooms out
  - [ ] Ctrl+Esc resets zoom
  - [ ] Win+Plus does NOT zoom (passes through to OS)
- [ ] Change modifier to **Alt** in settings:
  - [ ] Alt+Plus zooms in
  - [ ] Alt+Minus zooms out
  - [ ] Alt+Esc resets zoom
- [ ] **Win+Ctrl+M always works** regardless of configured modifier (opens settings)
- [ ] After changing modifier, scroll-gesture still works with same modifier key

### WS2: Viewport Drift Fixes

- [ ] **Pointer timestamp (WS2A):** Zoom to 5x, make tiny mouse movements (within deadzone). Viewport should NOT jump to focus/caret target.
- [ ] **Transition cancellation (WS2B):** Zoom to 3x, Tab to move focus, then immediately move mouse. Viewport should snap to cursor instantly, not continue drifting toward focus target.
- [ ] **Modifier key filter (WS2C):** Zoom to 3x, hold Ctrl for 2 seconds without typing. Viewport should remain pointer-tracking, not switch to caret.
- [ ] **Combined test:** Zoom to 5x, type in a text editor (caret tracking activates), then move mouse. Viewport should smoothly transition back to pointer tracking without drift.

### WS3: Diagnostic Logger

- [ ] Build with `SMOOTHZOOM_LOGGING=ON` (default for Debug)
- [ ] Open DebugView or VS Output window
- [ ] Verify log messages appear with format `[SmoothZoom:Component] LVL: message`
- [ ] Verify: session lock/unlock messages appear
- [ ] Verify: MagBridge failure/recovery messages appear (if applicable)
- [ ] Build with `SMOOTHZOOM_LOGGING=OFF` — verify no log output

## 2. Remaining Phase 6 ACs

### Color Inversion (AC-2.10.01–AC-2.10.05)

- [x] Ctrl+Alt+I toggles color inversion on/off (instantaneous, no animation) — 2026-06-18
- [ ] Inversion persists across zoom level changes
- [x] Inversion state survives settings save/reload — 2026-06-18 (config + restart)
- [ ] Inversion resets on application exit (screen returns to normal)
- [ ] Inversion works at 1x zoom (if applicable)

### Multi-Monitor (E6.4–E6.7)

- [ ] Viewport tracks cursor across monitor boundaries
- [ ] No viewport jump when cursor crosses monitor edges
- [ ] WM_DISPLAYCHANGE updates virtual screen metrics correctly
- [ ] Zoom works on non-primary monitor

### Conflict Detection (AC-ERR.01, E6.8)

- [ ] Launch Windows Magnifier first, then SmoothZoom — dialog appears
- [ ] Click "Yes" to close Magnifier — SmoothZoom starts normally
- [ ] Click "No" — SmoothZoom exits cleanly

### Crash Recovery (R-14, E6.11)

- [ ] Kill SmoothZoom via Task Manager while zoomed — next launch resets magnification
- [ ] Clean exit removes sentinel file (no false crash detection on next start)
- [ ] WM_ENDSESSION (shutdown/logoff) resets zoom

### Performance (E6.12)

- [ ] Build with SMOOTHZOOM_PERF_AUDIT — verify frame timing logs in DebugView
- [x] Zoomed + pointer stationary: CPU < 2% — 2026-06-18 measured 0.00% (dev hw, provisional)
- [x] Zoomed + pointer moving: CPU < 5% — 2026-06-18 measured 0.06% (dev hw, provisional)

## 3. Core Feature Spot-Check (Regression)

- [x] Win+Scroll: smooth zoom in/out (120-unit increments) — 2026-06-18 (Shift+Scroll per config; centered on pointer)
- [x] Win+Esc: animated reset to 1x — 2026-06-18 (Shift+Esc per config)
- [ ] Win+Plus/Minus: keyboard zoom with ease-out animation
- [ ] Scroll interrupts keyboard animation (AC-2.2.07)
- [ ] Start Menu suppression when Win is modifier (AC-2.1.16)
- [ ] Toggle: Ctrl+Alt hold engages, release returns to previous zoom
- [ ] Tray icon: right-click menu (Settings, Toggle Zoom, Exit)
- [ ] Settings window: changes apply without restart (AC-2.9.04)
- [ ] Settings persistence: changes survive restart (AC-2.9.02)
- [ ] Tray "Exit": graceful exit animates zoom to 1.0× before closing (AC-2.9.16)
- [ ] No global keyboard exit: Ctrl+Q in any app does NOT quit SmoothZoom

## 4. Deferred ACs

These are documented as out-of-scope until Desktop Duplication API migration:

- **AC-2.3.08** — Nearest-neighbor filtering option
- **AC-2.3.09** — Image smoothing toggle
- **AC-2.9.07** — Settings UI for image smoothing (retained in schema for forward-compat)

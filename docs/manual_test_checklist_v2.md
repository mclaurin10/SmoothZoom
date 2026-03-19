# SmoothZoom — Manual Test Checklist v2

Phase 6 polish: bug-fix verification, remaining ACs, core regression spot-check.

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

- [ ] Ctrl+Alt+I toggles color inversion on/off (instantaneous, no animation)
- [ ] Inversion persists across zoom level changes
- [ ] Inversion state survives settings save/reload
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
- [ ] Zoomed + pointer stationary: CPU < 2%
- [ ] Zoomed + pointer moving: CPU < 5%

## 3. Core Feature Spot-Check (Regression)

- [ ] Win+Scroll: smooth zoom in/out (120-unit increments)
- [ ] Win+Esc: animated reset to 1x
- [ ] Win+Plus/Minus: keyboard zoom with ease-out animation
- [ ] Scroll interrupts keyboard animation (AC-2.2.07)
- [ ] Start Menu suppression when Win is modifier (AC-2.1.16)
- [ ] Toggle: Ctrl+Alt hold engages, release returns to previous zoom
- [ ] Tray icon: right-click menu (Settings, Toggle Zoom, Exit)
- [ ] Settings window: changes apply without restart (AC-2.9.04)
- [ ] Settings persistence: changes survive restart (AC-2.9.02)
- [ ] Ctrl+Q: graceful exit with zoom reset

## 4. Deferred ACs

These are documented as out-of-scope until Desktop Duplication API migration:

- **AC-2.3.08** — Nearest-neighbor filtering option
- **AC-2.3.09** — Image smoothing toggle
- **AC-2.9.07** — Settings UI for image smoothing (retained in schema for forward-compat)

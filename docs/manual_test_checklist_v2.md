# SmoothZoom — Manual Test Checklist (v2)

**The single live verification tracker** for the Phase 7 v1.0 Release Verification campaign
(`07_v1.0_Release_Verification_PRD.md`). The earlier `manual_test_checklist.md` has been
archived to `docs/archive/`; its findings are carried into §5–§6 below. Sections 1–3 are the
Phase 6 bug-fix/regression spot-checks; §5 is the full 139-AC tracker; §6 lists the
carried-over findings to confirm or close.

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

> **Verification log — 2026-06-18 (computer-use pass; signed Release build running,
> single 1440×900 display, config modifier = Shift, toggle combo = Win+Ctrl):**
> - **Unit suite:** `smoothzoom_tests.exe` → **All tests passed (422 assertions / 146
>   cases)** — confirms every UT-marked AC (zoom math/bounds/soft-clamp, animation
>   retarget/reverse/interrupt, proportional mapping/corners/deadzone, settings
>   validation, ScrollNormalizer, SeqLock, rect validation). ✅
> - **Live `config.json`** valid, `schemaVersion: 1`, `scrollSensitivity`/`momentumZoom`
>   present → AC-2.9.01/02 + schema versioning. Native Magnifier not running. ✅
> - **Keyboard zoom-in** (Shift+= ×11) magnified the screen and animated; observed via
>   screenshot of Notepad scaling up. AC-2.8.01/2.8.06/2.2.04. ✅
> - **No character leak:** after 11 zoom-key presses with Shift held, Notepad stayed at
>   "Col 51, 50 characters" — the four zoom keys are consumed on modifier-held (no `+`/`_`
>   leak). ✅
> - **Zoom centered on pointer** — at high zoom the I-beam stayed dead-center
>   (proportional mapping). AC-2.1.09 / AC-2.4.01; cursor visible+correct AC-2.3.03;
>   uniform magnification AC-2.3.01. ✅
> - **Color inversion Ctrl+Alt+I** toggled on AND off while zoomed, and **at 1.0×** the
>   whole desktop inverted (wallpaper/Notepad/cursor). AC-2.10.01/02/05, E6.3. ✅
> - **Reset:** Shift+Esc animated back to exactly 1.0×, no residual tint/artifact.
>   AC-2.8.08 / AC-2.3.12. ✅
> - **Minor finding:** the `Alt` in `Ctrl+Alt+I` passes through to the focused app (modern
>   Notepad showed its ribbon KeyTips) — by design, only the four zoom keys are consumed.
>   No text leaked; note that in some apps `Alt+I` could hit a menu accelerator.
> - **Not testable here (handed to user):** scroll-gesture zoom (computer-use scroll does
>   not reach the LL hook — already confirmed via SendInput earlier this day); multi-monitor
>   (single display); UAC/secure-desktop, lock/unlock, sleep/resume (need elevation/hardware);
>   focus/caret following (disabled in this machine's config).

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

---

## 5. Full Acceptance-Criteria Tracker (139 ACs) — VER-1

The authoritative AC list is Document 2. This table is the v1.0 verification record. Status
legend:

- `✅ 06-18` — verified on the signed v1.0 build, 2026-06-18 (`(cu)` = confirmed via the computer-use pass)
- `UT` — passing via automated unit test (Catch2)
- `prior` — passed in earlier ad-hoc testing on a pre-release build; **needs a recorded Phase-7 confirmation** against the signed build
- `⚠ open` — carried finding to confirm or close (see §6)
- `⊘ def` — deferred or reframed (see §4 and Doc 2 §15)
- `—` — not yet exercised

### 2.1 Scroll-Gesture Zoom (21)

| AC | Description | Status |
|----|-------------|--------|
| AC-2.1.01 | Win+Scroll up zooms in, down zooms out | ✅ 06-18 |
| AC-2.1.02 | Scroll without modifier passes through to apps | prior |
| AC-2.1.03 | Releasing modifier holds zoom level (no snap-back) | prior |
| AC-2.1.04 | Both LWin and RWin work as modifier | — (no dual-Win keyboard) |
| AC-2.1.05 | Precision touchpad two-finger scroll zooms smoothly | prior / UT (ScrollNormalizer) |
| AC-2.1.06 | Logarithmic model: 1×→2× same effort as 2×→4× | UT |
| AC-2.1.07 | Zoom rate scales with scroll velocity | prior |
| AC-2.1.08 | Single notch ≈ 10–20% visible change | prior |
| AC-2.1.09 | Zoom-in keeps point under pointer fixed | ✅ 06-18 |
| AC-2.1.10 | Moving pointer during scroll uses current position as focal | prior |
| AC-2.1.11 | Pointer at center: symmetrical zoom, no drift | ⚠ open (occasional drift) |
| AC-2.1.12 | Pointer near corner: viewport stays within bounds | prior |
| AC-2.1.13 | Zoom doesn't go below min (1.0×) | UT + prior |
| AC-2.1.14 | Zoom doesn't exceed max (10.0×) | UT + prior |
| AC-2.1.15 | Soft bounds decelerate near limits | UT + prior |
| AC-2.1.16 | Win+Scroll then release Win: Start Menu suppressed | ⚠ open (intermittent, R-06) |
| AC-2.1.17 | Win press+release without scroll: Start Menu opens | prior |
| AC-2.1.18 | Win+Scroll then Win+E: Explorer opens normally | prior |
| AC-2.1.19 | Changing modifier in settings takes effect immediately | ⚠ open (earlier: no effect) |
| AC-2.1.20 | Non-Win modifier: no Start Menu suppression logic | ⚠ open (clarify expected) |
| AC-2.1.21 | Ctrl-exclusion rationale + alt-modifier warning | ⊘ def (reframed, Doc 2 §15.2) |

### 2.2 Smooth Zoom Animation (10)

| AC | Description | Status |
|----|-------------|--------|
| AC-2.2.01 | Scroll zoom renders next frame (≤16 ms) | prior |
| AC-2.2.02 | No visible frame drops under normal workloads | prior |
| AC-2.2.03 | Scroll zoom direct/continuous, no lag | prior |
| AC-2.2.04 | Keyboard zoom animates over 100–150 ms | UT + prior |
| AC-2.2.05 | Ease-out timing | UT |
| AC-2.2.06 | Rapid keyboard presses retarget smoothly | UT + prior |
| AC-2.2.07 | Plus then Minus reverses smoothly | UT + prior |
| AC-2.2.08 | Scroll interrupts keyboard animation | UT + prior |
| AC-2.2.09 | Toggle interrupts ongoing animation | UT + prior |
| AC-2.2.10 | Viewport + zoom move as one coordinated motion | prior |

### 2.3 Full-Screen Magnification (13; 2 deferred)

| AC | Description | Status |
|----|-------------|--------|
| AC-2.3.01 | All content magnified uniformly | ✅ 06-18 |
| AC-2.3.02 | No unmagnified holes | prior |
| AC-2.3.03 | Cursor visible + correctly positioned | ✅ 06-18 (cu) |
| AC-2.3.04 | Clicks interact with correct elements | prior |
| AC-2.3.05 | Drag operations correct while zoomed | prior |
| AC-2.3.06 | Hover/tooltips at correct position | prior |
| AC-2.3.07 | Image smoothing ON (always on — API) | prior |
| AC-2.3.08 | Image smoothing OFF: hard pixel edges | ⊘ def (R-01) |
| AC-2.3.09 | Toggling smoothing changes next frame | ⊘ def (R-01) |
| AC-2.3.10 | Native refresh rate, no tearing | prior |
| AC-2.3.11 | Real-time content updates smoothly | prior |
| AC-2.3.12 | At 1.0×: no artifact/overlay/tint | ✅ 06-18 (cu) |
| AC-2.3.13 | At 1.0×: no input latency/interception | prior |

### 2.4 Continuous Viewport Tracking (13)

| AC | Description | Status |
|----|-------------|--------|
| AC-2.4.01 | Proportional mapping | ✅ 06-18 / UT |
| AC-2.4.02 | Move right reveals content right | prior |
| AC-2.4.03 | Continuous mapping, no steps | — |
| AC-2.4.04 | Never pans beyond desktop bounds | UT |
| AC-2.4.05 | All four corners reachable | UT |
| AC-2.4.06 | Movement smooth, no jumps | prior |
| AC-2.4.07 | No perceptible tracking lag | prior |
| AC-2.4.08 | Stops immediately when pointer stops | — |
| AC-2.4.09 | Micro-movements don't jitter (deadzone) | UT + prior |
| AC-2.4.10 | Deadzone imperceptible during intentional movement | — |
| AC-2.4.11 | Deadzone scales with resolution | UT |
| AC-2.4.12 | Tracking identical at all zoom levels | UT + prior |
| AC-2.4.13 | At 10.0×: still smooth/controllable | — |

### 2.5 Keyboard Focus Following (14) — needs cross-app matrix (Doc 4 §6.3.5)

| AC | Description | Status |
|----|-------------|--------|
| AC-2.5.01–14 | Tab/Shift+Tab/Alt+Tab/arrow pans; already-visible suppression; partial-visibility margin; 100 ms debounce; rapid-tab; focus/pointer cooperation; cross-app matrix; silent no-op without UIA | UT (logic) ; MAN — pending sweep |

### 2.6 Text Cursor Following (11) — needs cross-app matrix

| AC | Description | Status |
|----|-------------|--------|
| AC-2.6.01–11 | Typing/arrows/Home/End/PgUp-Dn/click/Find pan; lookahead; caret priority while typing; 500 ms idle handoff; cross-app matrix; silent no-op | UT (logic) ; MAN — pending sweep |

### 2.7 Temporary Zoom Toggle (12)

| AC | Description | Status |
|----|-------------|--------|
| AC-2.7.01–12 | Peek to/from 1.0× and to last-used; brief tap; toggle-during-scroll; scroll-during-toggle; toggle-during-animation; configurable combo; no conflict with modifier | UT (8) ; MAN — pending |

### 2.8 Keyboard Shortcuts (12)

| AC | Description | Status |
|----|-------------|--------|
| AC-2.8.01–07 | Win+Plus/Minus step, clamps, cumulative retarget | UT + ✅ 06-18 (cu: zoom-in) |
| AC-2.8.08 | Modifier+Esc while zoomed: animate to 1.0× | ✅ 06-18 |
| AC-2.8.09 | Modifier+Esc at 1.0×: no effect | UT |
| AC-2.8.10 | Esc saves pre-escape level as last-used | — |
| AC-2.8.11 | Win+Ctrl+M opens settings window | — |
| AC-2.8.12 | Keyboard shortcut during Win+Scroll: step applied | — |

### 2.9 Settings & Configuration (18; 1 deferred)

| AC | Description | Status |
|----|-------------|--------|
| AC-2.9.01 | Settings saved to JSON in %AppData%\SmoothZoom\ | UT + prior |
| AC-2.9.02 | Loaded on startup; defaults if missing | UT + ✅ 06-18 (schema migrate) |
| AC-2.9.03 | Corrupt config: defaults, no crash | UT |
| AC-2.9.04 | Modifier change takes effect immediately | ⚠ open (see AC-2.1.19) |
| AC-2.9.05 | Reduce max while above: animate down | UT |
| AC-2.9.06 | Raise min while below: animate up | UT |
| AC-2.9.07 | Image-smoothing toggle | ⊘ def (R-01) |
| AC-2.9.08 | Toggle focus following off | — |
| AC-2.9.09 | Toggle caret following off | — |
| AC-2.9.10 | UI prevents min > max | UT |
| AC-2.9.11 | UI prevents toggle-key = modifier conflict | — |
| AC-2.9.12 | Keyboard step constrained 5–100% | UT |
| AC-2.9.13 | Tray icon visible while running | — |
| AC-2.9.14 | Right-click tray: Settings, Toggle, Exit | — |
| AC-2.9.15 | Tray "Toggle Zoom" | UT + — |
| AC-2.9.16 | Tray "Exit": animate to 1.0× then exit | — |
| AC-2.9.17 | Start with Windows | — |
| AC-2.9.18/19 | Start zoomed on/off | — |

### 2.10 Color Inversion (5)

| AC | Description | Status |
|----|-------------|--------|
| AC-2.10.01 | Ctrl+Alt+I toggles instantly | ✅ 06-18 |
| AC-2.10.02 | Applies to all content incl. cursor | ✅ 06-18 (cu) |
| AC-2.10.03 | Toggle from settings UI: same effect | ✅ 06-18 |
| AC-2.10.04 | State persists across sessions | ✅ 06-18 |
| AC-2.10.05 | Works at 1.0× | ✅ 06-18 (cu) |

### Multi-Monitor Basics (4) — needs second display

| AC | Description | Status |
|----|-------------|--------|
| AC-MM.01 | Zoom + track on pointer's monitor (primary clause) | — ; secondary "others at 1.0×" ⊘ def (R-01) |
| AC-MM.02 | Pointer transfer: zoom follows | — |
| AC-MM.03 | Different DPI: zoom relative to native content | — |
| AC-MM.04 | Different resolutions: tracking adapts | — |

### Conflict & Error Handling (5)

| AC | Description | Status |
|----|-------------|--------|
| AC-ERR.01 | Detect native Magnifier, show dialog | ⚠ open (dialog OK; post-close scroll path — E6.8) |
| AC-ERR.02 | API init failure: clear error, graceful exit | — |
| AC-ERR.03 | Hook deregistered: auto re-register + tray notice | — |
| AC-ERR.04 | Secure desktop: no crash, resume on return | — |
| AC-ERR.05 | Display config change while zoomed: no crash, adapt | — |

---

## 6. Carried-Over Findings to Confirm or Close (from archived v1)

Each must end Phase 7 as PASS, fixed, or a documented known-limitation (mirrors Doc 07 §8):

1. **AC-2.1.11 — viewport drift.** Occasional shift away from the pointer at zoom. Drift fixes (WS2) have since landed — re-verify; add diagnostic logging if it recurs.
2. **AC-2.1.16 / R-06 — Start Menu suppression.** "Mostly works, sometimes still triggers." Reproduce and fix or document the conflict.
3. **AC-2.1.19 / AC-2.9.04 — live modifier change.** Earlier report: changing the modifier in settings had no apparent effect. The hook now reads the live modifier — verify end-to-end.
4. **AC-2.1.20/21 — Ctrl-exclusion reframe.** `Ctrl` is excluded as a scroll modifier; AC-2.1.21 documents the rationale, not Ctrl-as-modifier behavior (Doc 2 §15.2). AC-2.1.20 ("no suppression for non-Win modifier") is expected behavior — confirm the test interpretation.
5. **E6.8 — post-conflict scroll path.** After the conflict dialog closes the other magnifier, scroll-to-zoom reportedly didn't function. Verify the init path after a detected conflict.
6. **Color-inversion persistence (was a v1 fail).** Now PASSES as of 2026-06-18 (config persist + restart round-trip) — closed.

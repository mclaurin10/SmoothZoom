# Phased Delivery Plan

## macOS-Style Full-Screen Zoom for Windows

### Document 4 of 5 — Development Plan Series

**Version:** 1.0
**Status:** Draft
**Last Updated:** February 2026
**Prerequisites:** Document 1 — Project Scope (v1.1), Document 2 — Behavior Specification (v1.0), Document 3 — Technical Architecture (v1.0)

---

## 1. Phasing Principles

The work is divided into seven phases. Each phase produces a runnable build that can be launched, tested, and evaluated independently of subsequent phases. Phases are ordered by three criteria applied in priority order:

1. **Risk retirement.** Work that could invalidate fundamental assumptions (e.g., "Does the Magnification API actually support smooth float-precision zoom?") comes first. If a phase reveals a blocking problem, the investment in subsequent phases has been minimized.

2. **Dependency graph.** Each phase builds only on components delivered in prior phases. No phase requires forward references to unbuilt work.

3. **User value.** Within the constraints of risk and dependency, earlier phases deliver more impactful capabilities. After Phase 1 completes, the application already provides the core experience that no other Windows tool offers.

Phases do not have calendar durations. Each phase is complete when all of its exit criteria are met, regardless of elapsed time. The exit criteria are expressed as specific acceptance criteria from the Behavior Specification (Document 2), identified by their AC numbers.

---

## 2. Phase Overview

| Phase | Name | What It Proves | Runnable Build Delivers |
|-------|------|---------------|------------------------|
| 0 | Foundation and Risk Spike | The Magnification API accepts float zoom with sub-frame latency. Global hooks intercept scroll reliably. UIAccess signing works. | A hardcoded test harness that zooms via scroll with a fixed modifier key. No animation, no settings, no tray icon. |
| 1 | Core Scroll-Gesture Zoom | The primary value proposition works end to end: smooth scroll zoom with proportional viewport tracking. | A useful magnifier. Hold Win+Scroll to zoom, viewport follows pointer, Start Menu suppressed correctly. |
| 2 | Keyboard Shortcuts and Animation | Zoom can be controlled from the keyboard with smooth animated transitions that interrupt and retarget cleanly. | Phase 1 + `Win+Plus`, `Win+Minus`, `Win+Esc`, all animated with ease-out. |
| 3 | Accessibility Tracking | The viewport follows keyboard focus and text cursor across applications, with correct priority arbitration. | Phase 2 + focus following on Tab/Alt+Tab, caret following while typing, smooth source transitions. |
| 4 | Temporary Toggle | Hold-to-peek interaction works, including all edge cases around interruption and state preservation. | Phase 3 + Ctrl+Alt hold-to-toggle with bidirectional peek. |
| 5 | Settings, Tray, and Persistence | All configuration is user-adjustable, persisted, and accessible from a system tray UI. | Phase 4 + tray icon, settings window, config.json, auto-start, configurable modifier/shortcuts. |
| 6 | Polish, Multi-Monitor, and Hardening | Edge cases, error recovery, multi-monitor support, color inversion, and crash safety. | The release candidate. |

---

## 3. Phase 0 — Foundation and Risk Spike

### 3.0.1 Purpose

This phase exists to retire the three highest-risk technical assumptions before any significant code is written. Every subsequent phase depends on these assumptions holding true. If any of them fails, the project's technical approach must be revised (see Document 5, Technical Risks and Mitigations) before proceeding.

### 3.0.2 Assumptions Under Test

**Assumption 1 — Float-precision magnification.** The Magnification API's `MagSetFullscreenTransform` accepts a `float` magnification level. The assumption is that the API honors sub-integer values (e.g., 1.07, 2.34, 5.891) and that the visual result changes smoothly when the level is incremented in small steps across consecutive frames. If the API internally quantizes to coarse steps, smooth scroll-gesture zoom is impossible through this API.

**Assumption 2 — Sub-frame API latency.** Calling `MagSetFullscreenTransform` and having the result appear on the next VSync must take less than one frame period. If the API introduces its own buffering or delays the update by one or more additional frames, the ≤16ms latency target (AC-2.2.01) cannot be met.

**Assumption 3 — Reliable scroll interception with UIAccess.** A global `WH_MOUSE_LL` hook installed from a process with `uiAccess="true"` can intercept and consume `WM_MOUSEWHEEL` messages system-wide, including over elevated (administrator) windows. The consumed events do not reach the target application, and non-consumed events pass through with no added latency.

### 3.0.3 Deliverable

A single-file C++ test harness (no installer, no tray icon, no settings) that:

- Initializes the Magnification API.
- Installs a low-level mouse hook.
- Monitors the state of the left `Win` key via `GetAsyncKeyState`.
- When `Win` is held and scroll events arrive: accumulates delta, computes a new zoom level (simple multiply, no logarithmic model yet), calls `MagSetFullscreenTransform` with the new level and a hardcoded center offset.
- When `Win` is released: stops processing scroll events but retains the current zoom level.
- On `Ctrl+Q`: resets zoom to 1.0× and exits.

This harness intentionally omits: animation, proportional viewport tracking, Start Menu suppression, keyboard shortcuts, settings, and all polish. It is the minimum code needed to validate the three assumptions.

### 3.0.4 Build Requirements

- The binary must be compiled as x64.
- The binary must include a manifest with `uiAccess="true"` and `PerMonitorV2` DPI awareness.
- The binary must be signed (self-signed certificate is sufficient for development).
- The binary must be executed from a secure folder (e.g., `C:\Program Files\SmoothZoom\`).

### 3.0.5 Exit Criteria

| # | Criterion | Validates |
|---|-----------|-----------|
| E0.1 | Calling `MagSetFullscreenTransform(1.5, 0, 0)` produces a visibly magnified screen at a level clearly between 1× and 2×. | Assumption 1 |
| E0.2 | Calling `MagSetFullscreenTransform` in a tight loop with values incrementing by 0.01 per frame produces a visibly smooth zoom animation with no stepping or quantization. | Assumption 1 |
| E0.3 | The visual zoom change appears on the display within one frame of the API call, verified by visual inspection and optionally by high-speed camera or frame timestamp logging. | Assumption 2 |
| E0.4 | Holding Win and scrolling the mouse wheel causes the screen to zoom. The zoom tracks scroll input responsively. | Assumption 3 |
| E0.5 | Releasing Win and scrolling does NOT zoom. Scroll events reach the foreground application normally. | Assumption 3 |
| E0.6 | The hook intercepts scroll events over an elevated window (e.g., Task Manager run as administrator). | Assumption 3 |

### 3.0.6 Risk Gate

**If E0.1 or E0.2 fails** (API quantizes zoom levels): Investigate whether the quantization is coarse enough to be visually unacceptable. If so, the MagBridge component must be redesigned around the Desktop Duplication API or Windows.Graphics.Capture (see Architecture §6). This is a significant scope increase.

**If E0.3 fails** (API adds latency): Investigate whether the delay is consistent (one extra frame) or variable. A consistent one-frame delay is tolerable if the total pipeline remains under 32ms. Variable delay is more problematic and may require the alternative rendering path.

**If E0.4, E0.5, or E0.6 fails**: Investigate hook installation order, `uiAccess` configuration, and whether the scroll event is being consumed at a different layer. These are more likely configuration issues than fundamental blockers.

---

## 4. Phase 1 — Core Scroll-Gesture Zoom

### 4.1.1 Purpose

Deliver the complete, polished scroll-gesture zoom experience — the feature that defines SmoothZoom and that no other Windows tool provides. At the end of this phase, the application is genuinely useful as a daily-driver magnifier for users who only need scroll-gesture zoom and pointer tracking.

### 4.1.2 Components Built

| Component | Scope in This Phase |
|-----------|-------------------|
| **MagBridge** | Full implementation. All six API wrappers (init, uninit, set/get transform, input transform, show cursor). Error logging. Shutdown cleanup sequence. Crash handler to reset zoom on unhandled exception. |
| **InputInterceptor** | Full `WH_MOUSE_LL` hook (scroll consumption, pointer position tracking). `WH_KEYBOARD_LL` hook limited to modifier key state observation — no shortcut processing yet. Hook health watchdog. |
| **WinKeyManager** | Full implementation. Win key state machine with Start Menu suppression via `SendInput` Ctrl injection. Disabled when modifier is not Win. |
| **ZoomController** | `SCROLL_DIRECT` and `IDLE` modes only. Logarithmic scroll-to-zoom model. Soft-approach bounds clamping. No `ANIMATING` or `TOGGLING` modes yet. |
| **ViewportTracker** | Pointer-tracking only. Proportional mapping algorithm. Edge clamping. Deadzone for micro-jitter suppression. No focus or caret tracking yet. |
| **RenderLoop** | Full frame-tick loop on the Render Thread. `DwmFlush` synchronization. Reads scroll accumulator, calls ZoomController, calls ViewportTracker, calls MagBridge. Skips redundant API calls when values haven't changed. |
| **Shared state** | Scroll delta accumulator (atomic), pointer position (atomic pair / SeqLock), modifier key state (atomic). |

### 4.1.3 Acceptance Criteria Covered

**Scroll-Gesture Zoom (§2.1):** AC-2.1.01 through AC-2.1.18 — the entire scroll-gesture zoom and Start Menu suppression specification.

**Smooth Zoom Animation (§2.2):** AC-2.2.01, AC-2.2.02, AC-2.2.03, AC-2.2.10 — scroll-gesture smoothness and viewport pan synchronization. (Keyboard animation criteria are deferred to Phase 2.)

**Full-Screen Magnification (§2.3):** AC-2.3.01 through AC-2.3.13 — all content coverage, cursor behavior, image smoothing, rendering quality, and 1.0× transparency criteria.

**Continuous Viewport Tracking (§2.4):** AC-2.4.01 through AC-2.4.13 — all proportional mapping, edge behavior, smoothness, deadzone, and zoom-level-scaling criteria.

**Error Handling:** AC-ERR.02 (Magnification API init failure), AC-ERR.03 (hook health recovery), AC-ERR.04 (secure desktop resilience) — partial. Enough error handling to prevent crashes during normal use.

### 4.1.4 Exit Criteria

| # | Criterion | Key ACs |
|---|-----------|---------|
| E1.1 | Hold Win + scroll up: screen zooms in smoothly, centered on pointer position. | AC-2.1.01, AC-2.1.09 |
| E1.2 | Hold Win + scroll down: screen zooms out smoothly. At 1.0×, further scroll-down has no effect. | AC-2.1.01, AC-2.1.13 |
| E1.3 | Zoom at 10.0× cannot be exceeded. The zoom decelerates into the bound, not a hard stop. | AC-2.1.14, AC-2.1.15 |
| E1.4 | Release Win after scrolling: Start Menu does NOT open. | AC-2.1.16 |
| E1.5 | Press and release Win WITHOUT scrolling: Start Menu opens normally. | AC-2.1.17 |
| E1.6 | Press Win, scroll, then press E (still holding Win): Explorer opens normally. | AC-2.1.18 |
| E1.7 | While zoomed, move pointer: viewport glides proportionally. No lag, no jitter, no snapping. | AC-2.4.01, AC-2.4.06, AC-2.4.07 |
| E1.8 | Pointer at screen corner: viewport shows desktop corner. All four corners reachable. | AC-2.4.04, AC-2.4.05 |
| E1.9 | Pointer nearly stationary: viewport does not jitter. | AC-2.4.09 |
| E1.10 | While zoomed, click a button: the correct button activates. Drag a window: it moves correctly. | AC-2.3.04, AC-2.3.05 |
| E1.11 | With Win released, scroll in a browser: the page scrolls normally. No interference. | AC-2.1.02 |
| E1.12 | At 1.0× zoom, no visual artifacts, no performance impact, no input latency. | AC-2.3.12, AC-2.3.13 |
| E1.13 | Zooming from 1× to 2× takes roughly the same scroll effort as zooming from 5× to 10×. | AC-2.1.06 |
| E1.14 | Two-finger touchpad scroll with Win held produces smooth zoom (if Precision Touchpad available). | AC-2.1.05 |

### 4.1.5 What Is NOT in This Phase

- Keyboard shortcuts (`Win+Plus`, `Win+Minus`, `Win+Esc`).
- Animated transitions (all zoom changes are direct/immediate from scroll input).
- Focus following and caret following.
- Temporary toggle.
- Settings UI, tray icon, config.json.
- Color inversion.
- Multi-monitor handling.
- Configurable modifier key (hardcoded to Win).
- Graceful exit UI (exit via a hardcoded key like `Ctrl+Q`, or Task Manager).

---

## 5. Phase 2 — Keyboard Shortcuts and Animation

### 5.2.1 Purpose

Add keyboard-driven zoom control with smooth animated transitions. This phase introduces the animation engine into the RenderLoop and the `ANIMATING` mode into ZoomController. It also adds the keyboard command pathway through InputInterceptor.

### 5.2.2 Components Modified or Built

| Component | Changes |
|-----------|---------|
| **InputInterceptor** | `WH_KEYBOARD_LL` callback now detects and posts keyboard shortcut commands: `ZOOM_IN` (`Win+Plus`/`Win+=`), `ZOOM_OUT` (`Win+Minus`), `ZOOM_RESET` (`Win+Esc`). Keyboard command lock-free queue introduced. |
| **ZoomController** | `ANIMATING` mode added. Additive keyboard step calculation. Target retargeting (new step merges with in-flight animation). Mode interruption: scroll gesture overrides in-progress keyboard animation. |
| **RenderLoop** | Frame tick now drains the keyboard command queue. Ease-out interpolation added to the frame tick for `ANIMATING` mode. Zoom and offset interpolated as a coordinated pair. |
| **ViewportTracker** | Zoom-center computation for keyboard-initiated zoom. When a keyboard shortcut changes the zoom, the zoom center is the current pointer position (same as scroll). The proportional mapping formula handles this naturally — no special case needed. |

### 5.2.3 Acceptance Criteria Covered

**Smooth Zoom Animation (§2.2):** AC-2.2.04 through AC-2.2.10 — keyboard animation timing, ease-out curve, target retargeting, direction reversal, scroll interruption, and pan synchronization.

**Keyboard Shortcuts (§2.8):** AC-2.8.01 through AC-2.8.10 — zoom in/out steps, bounds clamping, rapid press accumulation, and toggle-off via `Win+Esc`.

### 5.2.4 Exit Criteria

| # | Criterion | Key ACs |
|---|-----------|---------|
| E2.1 | Press `Win+Plus`: zoom animates smoothly from current level to current+step over ~150ms with ease-out. No snap. | AC-2.2.04, AC-2.2.05, AC-2.8.01 |
| E2.2 | Press `Win+Minus`: zoom animates down. At 1.0×, no effect. | AC-2.8.02, AC-2.8.05 |
| E2.3 | Press `Win+Plus` three times rapidly: zoom animates smoothly to three steps above the starting level as a single continuous motion, not three sequential animations. | AC-2.2.06, AC-2.8.07 |
| E2.4 | Press `Win+Plus` then immediately `Win+Minus`: animation smoothly reverses. No bounce, stutter, or pause at the reversal. | AC-2.2.07 |
| E2.5 | During a keyboard animation, hold Win and scroll: scroll gesture takes over immediately. Zoom responds to scroll input, animation is abandoned. | AC-2.2.08 |
| E2.6 | Press `Win+Plus` while at 9.9×: zoom animates to 10.0× (clamped), not to 10.15×. | AC-2.8.03 |
| E2.7 | Press `Win+Esc` while zoomed: zoom animates smoothly to 1.0×. | AC-2.8.08 |
| E2.8 | Press `Win+Esc` while at 1.0×: no effect. | AC-2.8.09 |
| E2.9 | During all keyboard-initiated zoom changes, viewport panning is synchronized with the zoom change. No moment where zoom is ahead of or behind the viewport position. | AC-2.2.10 |

---

## 6. Phase 3 — Accessibility Tracking

### 6.3.1 Purpose

Deliver keyboard focus following and text cursor following — the features that make SmoothZoom an accessibility tool, not just a presentation aid. This is the most technically uncertain phase after Phase 0, because UIA behavior varies widely across applications.

### 6.3.2 Components Modified or Built

| Component | Changes |
|-----------|---------|
| **UIA Thread** | New thread. COM initialization. Message pump. Hosts FocusMonitor and CaretMonitor. |
| **FocusMonitor** | New component. UIA `AutomationFocusChangedEvent` subscription. Writes bounding rectangle to shared state via SeqLock. |
| **CaretMonitor** | New component. UIA TextPattern subscription (preferred path) and `GetGUIThreadInfo` polling at 30Hz (fallback path). Writes caret rectangle to shared state. |
| **ViewportTracker** | Major addition: multi-source tracking. `determineActiveSource()` priority arbitration logic. Source transition smoothing (200ms ease-out between sources). Focus debounce (100ms). Caret lookahead margin. Caret idle timeout (500ms) for pointer handoff. |
| **RenderLoop** | Frame tick now reads focus and caret shared state. Calls ViewportTracker's updated offset computation. |
| **Shared state** | New entries: focus rectangle (SeqLock), caret rectangle (SeqLock), last focus change timestamp (atomic), last keyboard input timestamp (already exists from Phase 1, now used for caret priority). |

### 6.3.3 Acceptance Criteria Covered

**Keyboard Focus Following (§2.5):** AC-2.5.01 through AC-2.5.14 — Tab, Shift+Tab, Alt+Tab, arrow key navigation, already-visible suppression, partial-visibility margin, debounce, rapid-tab handling, focus/pointer cooperation, and UIA compatibility matrix.

**Text Cursor Following (§2.6):** AC-2.6.01 through AC-2.6.11 — typing, arrow keys, Home/End/PageUp/Down, click-repositioned caret, Find & Replace jump, lookahead margin, priority over pointer during typing, idle handoff back to pointer, and compatibility matrix.

### 6.3.4 Exit Criteria

| # | Criterion | Key ACs |
|---|-----------|---------|
| E3.1 | While zoomed, press Tab: if the focused element is off-viewport, viewport pans smoothly to show it. | AC-2.5.01 |
| E3.2 | While zoomed, press Alt+Tab: viewport pans to the newly active window. | AC-2.5.03 |
| E3.3 | Focused element already visible: Tab produces no viewport motion. | AC-2.5.05 |
| E3.4 | Hold Tab to cycle rapidly through 10+ controls: viewport does NOT chase each one. It waits, then pans to the final focused control. | AC-2.5.07, AC-2.5.09 |
| E3.5 | After a focus pan, move the mouse: pointer tracking resumes immediately and smoothly. | AC-2.5.11 |
| E3.6 | While zoomed, type in Notepad: viewport keeps the caret visible. Caret never sits at the very edge of the viewport. | AC-2.6.01, AC-2.6.06 |
| E3.7 | While typing, move the mouse slightly: viewport stays on the caret (does not jump to pointer). | AC-2.6.07 |
| E3.8 | Stop typing for 500ms, then move the mouse: viewport transitions smoothly from caret position to pointer tracking. | AC-2.6.08 |
| E3.9 | While typing, press Tab to move to the next field: focus following takes over from caret following. | AC-2.6.09 |
| E3.10 | Press Home in a text editor: viewport pans smoothly to the beginning of the line. Press Ctrl+End: viewport pans to the end of the document. | AC-2.6.02, AC-2.6.03 |
| E3.11 | Focus following verified working in: Edge, Chrome, Firefox, File Explorer, Notepad, VS Code, Windows Settings. | AC-2.5.13 |
| E3.12 | Caret following verified working in: Notepad, Word, Chrome address bar, VS Code, Windows Terminal. | AC-2.6.10 |
| E3.13 | In an application that doesn't support UIA focus events (tested manually): no crash, no error, pointer tracking works normally. | AC-2.5.14 |

### 6.3.5 Application Compatibility Testing

This phase requires the most extensive manual testing of any phase, because UIA support varies across applications. The following matrix must be tested:

| Application | Focus Following | Caret Following | Notes |
|------------|----------------|-----------------|-------|
| Notepad | Must work | Must work | Uses standard Win32 controls |
| WordPad | Must work | Must work | Rich edit control |
| Microsoft Word | Must work | Must work | Custom UIA provider |
| Microsoft Excel | Must work | Must work (formula bar + cells) | Complex focus model |
| Google Chrome | Must work | Must work (address bar + web inputs) | Chromium UIA support |
| Mozilla Firefox | Must work | Must work | Gecko UIA support |
| Microsoft Edge | Must work | Must work | Chromium-based |
| File Explorer | Must work | N/A | No text editing |
| Windows Settings | Must work | Limited | WinUI controls |
| Visual Studio Code | Must work | Must work | Electron-based |
| Windows Terminal | Must work | Best effort | Pseudo-terminal rendering |
| Command Prompt | Best effort | Best effort | Legacy console host |

"Must work" = a failure here is a bug that blocks the phase. "Best effort" = a failure here is a known limitation documented for the user, not a blocker.

---

## 7. Phase 4 — Temporary Toggle

### 7.4.1 Purpose

Add the hold-to-peek interaction. This is a self-contained feature with well-defined state management. It is sequenced after accessibility tracking so that the toggle correctly interacts with all three tracking sources.

### 7.4.2 Components Modified

| Component | Changes |
|-----------|---------|
| **InputInterceptor** | Keyboard hook now detects the toggle key combination (default: `Ctrl+Alt`) press and release. Posts `TOGGLE_ENGAGE` and `TOGGLE_RELEASE` commands to the keyboard queue. Distinguishes between "both keys pressed" and "one key released" to handle staggered key release. |
| **ZoomController** | `TOGGLING` mode added. `savedZoomForToggle` state. On engage: save current zoom, set target to 1.0× (or to last-used/default if already at 1.0×). On release: restore target to saved zoom. Handles edge cases: toggle during scroll, toggle during animation (saves the mid-animation level), scroll during toggle (updates the restore target). |
| **RenderLoop** | No structural changes — toggle commands flow through the existing keyboard command drain and zoom interpolation. The animation engine already handles the `TOGGLING` mode's animated transitions. |

### 7.4.3 Acceptance Criteria Covered

**Temporary Zoom Toggle (§2.7):** AC-2.7.01 through AC-2.7.12 — peek at 1.0×, peek at zoomed, brief tap, toggle during scroll, scroll during toggle, toggle during animation, configurable combo, and conflict prevention with modifier key.

### 7.4.4 Exit Criteria

| # | Criterion | Key ACs |
|---|-----------|---------|
| E4.1 | While zoomed at 4.0×, hold Ctrl+Alt: zoom animates to 1.0× over ~250ms. Release: zoom animates back to 4.0×. | AC-2.7.01, AC-2.7.03 |
| E4.2 | While at 1.0×, hold Ctrl+Alt: zoom animates to last-used level (or default 2.0× on first use). Release: animates back to 1.0×. | AC-2.7.04, AC-2.7.05, AC-2.7.06 |
| E4.3 | Tap Ctrl+Alt very briefly (<100ms): full animation plays out — zoom to toggle state, then immediately back. Appears as a brief flash. No crash. | AC-2.7.07 |
| E4.4 | Scroll-zoom to 6.3×, then hold Ctrl+Alt, then release: returns to 6.3×, not to a rounded value. | AC-2.7.08 |
| E4.5 | Hold Ctrl+Alt (now at 1.0×), then Win+scroll to 3.0×, then release Ctrl+Alt: returns to 3.0× (the new level), not 6.3× (the pre-toggle level). | AC-2.7.09 |
| E4.6 | While a keyboard animation is in progress (e.g., Win+Plus just pressed), hold Ctrl+Alt: toggle captures the mid-animation zoom level. On release, returns to that mid-animation level. | AC-2.7.10 |

---

## 8. Phase 5 — Settings, Tray, and Persistence

### 8.5.1 Purpose

Make SmoothZoom a proper installed application with user-configurable settings, a system tray presence, persistent configuration, and auto-start capability. Before this phase, all settings are hardcoded; after it, every tunable value is user-adjustable.

### 8.5.2 Components Modified or Built

| Component | Changes |
|-----------|---------|
| **SettingsManager** | New component. Loads/validates/saves `config.json` from `%AppData%\SmoothZoom\`. Immutable snapshot model with atomic pointer swap. Observer notification to dependent components. Corrupt-file recovery (defaults + log warning). |
| **TrayUI** | New component. System tray icon via `Shell_NotifyIcon`. Right-click context menu: Settings, Toggle Zoom On/Off, Exit. Double-click opens settings. Exit triggers graceful zoom-to-1.0× animation before process termination. |
| **Settings window** | New UI. Single-panel dialog or window displaying all configurable values: modifier key, toggle combo, min/max zoom, keyboard step size, animation speed, image smoothing, follow focus toggle, follow cursor toggle, start with Windows, start zoomed, default zoom level. Validation per AC-2.9.10 through AC-2.9.12. Inline conflict warnings. |
| **InputInterceptor** | Now reads modifier key and shortcut bindings from the live SettingsSnapshot instead of hardcoded values. Responds to settings observer notifications to reload bindings. |
| **WinKeyManager** | Now enables/disables itself based on whether the configured modifier is Win. Responds to settings changes. |
| **ZoomController** | Reads min/max zoom, keyboard step size, and default zoom level from settings. Responds to live changes (e.g., max zoom lowered while zoomed above it → animate down). |
| **RenderLoop** | Reads animation speed and image smoothing preference from settings. |
| **MagBridge** | Reads image smoothing preference (bilinear vs. nearest-neighbor). Applies on next frame when toggled. |
| **Application startup** | Registers for auto-start via Registry `Run` key or Task Scheduler when enabled. Applies "start zoomed" on launch if configured. Opens settings shortcut: `Win+Ctrl+M`. |

### 8.5.3 Acceptance Criteria Covered

**Settings and Configuration (§2.9):** AC-2.9.01 through AC-2.9.19 — all persistence, immediate-effect, validation, tray, and startup criteria.

**Keyboard Shortcuts (§2.8):** AC-2.8.11 (`Win+Ctrl+M` opens settings).

**Modifier Key Configuration (§2.1):** AC-2.1.19 through AC-2.1.21 — configurable modifier, immediate effect, Ctrl-as-modifier trade-off documentation.

### 8.5.4 Exit Criteria

| # | Criterion | Key ACs |
|---|-----------|---------|
| E5.1 | Change modifier key from Win to Ctrl in settings: Ctrl+Scroll zooms immediately. Win+Scroll does nothing. No restart. | AC-2.1.19, AC-2.9.04 |
| E5.2 | Change max zoom to 5.0× while zoomed at 8.0×: zoom animates smoothly down to 5.0×. | AC-2.9.05 |
| E5.3 | Toggle image smoothing off in settings: magnified text shows sharp pixel edges on the next frame. Toggle on: edges are anti-aliased. | AC-2.3.07, AC-2.3.08, AC-2.9.07 |
| E5.4 | Exit SmoothZoom, re-launch: all settings are as they were. Config file exists in `%AppData%\SmoothZoom\`. | AC-2.9.01, AC-2.9.02 |
| E5.5 | Manually corrupt config.json (delete a required field, insert invalid JSON): application starts with defaults. No crash. | AC-2.9.03 |
| E5.6 | Right-click tray icon → "Toggle Zoom On/Off" while zoomed: animates to 1.0×. Click again: animates to last-used level. | AC-2.9.15 |
| E5.7 | Right-click tray icon → "Exit" while zoomed: zoom animates to 1.0×, then the process terminates. Screen is not left magnified. | AC-2.9.16 |
| E5.8 | Enable "Start with Windows", reboot, log in: SmoothZoom is running (tray icon visible). | AC-2.9.17 |
| E5.9 | Enable "Start zoomed" with default level 3.0×, exit and re-launch: screen zooms to 3.0× shortly after the application starts. | AC-2.9.18 |
| E5.10 | Attempt to set toggle key combo to the same key as the scroll-gesture modifier: settings UI prevents this and explains the conflict. | AC-2.9.11, AC-2.7.12 |
| E5.11 | Press `Win+Ctrl+M`: settings window opens. Press again: settings window comes to foreground. | AC-2.8.11 |
| E5.12 | With Ctrl configured as modifier, the settings UI notes that Ctrl+Scroll will be consumed and won't reach applications like browsers. | AC-2.1.21 |

---

## 9. Phase 6 — Polish, Multi-Monitor, and Hardening

### 9.6.1 Purpose

Close all remaining acceptance criteria, handle edge cases, harden error recovery, and bring the application to release-candidate quality. This phase does not introduce new user-facing concepts — it extends existing features to cover harder scenarios.

### 9.6.2 Work Items

#### 9.6.2a — Color Inversion

| Component | Changes |
|-----------|---------|
| **InputInterceptor** | Keyboard hook detects `Ctrl+Alt+I` and posts `TOGGLE_COLOR_INVERSION` command. |
| **MagBridge** | Applies or removes the 5×5 color inversion matrix via `MagSetFullscreenColorEffect`. |
| **SettingsManager** | Persists `colorInversionEnabled` state. Loads on startup. |

**Criteria covered:** AC-2.10.01 through AC-2.10.05.

#### 9.6.2b — Multi-Monitor

| Component | Changes |
|-----------|---------|
| **MagBridge** | Detects which monitor the pointer is on via `MonitorFromPoint`. When the active monitor changes, applies the transform to the new monitor. Handles monitors with different resolutions and DPI settings. |
| **ViewportTracker** | Adjusts proportional mapping, deadzone threshold, and edge clamping to the active monitor's geometry and DPI. |
| **RenderLoop** | Detects monitor transitions (pointer moved to a different monitor) and triggers a smooth handoff. |

**Criteria covered:** AC-MM.01 through AC-MM.04.

#### 9.6.2c — Display Configuration Changes

| Component | Changes |
|-----------|---------|
| **Main Thread** | Registers for `WM_DISPLAYCHANGE` and `WM_DPICHANGED` messages. On receipt, re-queries monitor geometry via `EnumDisplayMonitors` and `GetMonitorInfo`. Notifies ViewportTracker and MagBridge. |
| **ViewportTracker** | Re-validates the current viewport offset against the new screen geometry. Repositions if the current offset is out of bounds. |
| **MagBridge** | Re-queries screen dimensions. Adjusts input transform rectangles. |

**Criteria covered:** AC-ERR.05.

#### 9.6.2d — Conflict Detection

| Component | Changes |
|-----------|---------|
| **Application startup** | Before calling `MagInitialize`, checks for the native Magnifier process (`Magnify.exe`). If running, shows a dialog offering to close it or exit SmoothZoom. |
| **MagBridge** | After `MagInitialize`, calls `MagGetFullscreenTransform` to verify the current magnification state. If zoom is not 1.0× (another magnifier is active), warns the user. |

**Criteria covered:** AC-ERR.01, AC-ERR.02.

#### 9.6.2e — Crash Recovery and Safe Shutdown

| Component | Changes |
|-----------|---------|
| **Main Thread** | Installs `SetUnhandledExceptionFilter` handler. On unhandled exception: attempts `MagSetFullscreenTransform(1.0, 0, 0)` and `MagUninitialize()` before terminating. |
| **Main Thread** | On `WM_ENDSESSION` (system shutdown / user logoff): performs graceful zoom reset. |
| **MagBridge** | Writes a "dirty shutdown" sentinel file on init. Removes it on clean shutdown. On next startup, if the sentinel exists, warns the user that the previous session may have exited uncleanly. |

#### 9.6.2f — Hook Robustness

| Component | Changes |
|-----------|---------|
| **InputInterceptor** | Hook health watchdog timer (every 5 seconds) verifies hook handles are valid. If a hook has been silently unregistered (known Windows behavior under high system load), re-installs it. Displays a tray notification on re-registration failure. |

**Criteria covered:** AC-ERR.03, AC-ERR.04.

#### 9.6.2g — Performance Audit

Formal measurement against all performance criteria from Scope §6.2:

| Metric | Target | How Measured |
|--------|--------|-------------|
| Scroll-to-display latency | ≤16ms at 60Hz | High-speed camera or specialized latency measurement tool (e.g., NVIDIA LDAT, Is It Snappy) |
| CPU idle (zoomed, pointer still) | <2% | Windows Performance Monitor, 60-second sample |
| CPU active (zoomed, pointer moving) | <5% | Windows Performance Monitor, 60-second sample |
| GPU overhead | <10% on Intel UHD 620 | GPU-Z or Intel Graphics Monitor |
| Memory footprint | <50MB | Task Manager working set |

### 9.6.3 Exit Criteria

| # | Criterion | Key ACs |
|---|-----------|---------|
| E6.1 | Press `Ctrl+Alt+I`: screen colors invert instantly. Press again: colors return to normal. | AC-2.10.01 |
| E6.2 | Color inversion persists after exit and re-launch. | AC-2.10.04 |
| E6.3 | Color inversion works at 1.0× (screen inverted but not magnified). | AC-2.10.05 |
| E6.4 | On a dual-monitor setup, zoom is active on the monitor where the pointer resides. The other monitor is at 1.0×. | AC-MM.01 |
| E6.5 | Move pointer from monitor 1 to monitor 2 while zoomed: zoom transfers smoothly to monitor 2. | AC-MM.02 |
| E6.6 | Dual monitors with different DPI (100% and 150%): zoom level is relative to each monitor's native content. | AC-MM.03 |
| E6.7 | Dual monitors with different resolutions: viewport tracking, deadzone, and edge clamping work correctly on both. | AC-MM.04 |
| E6.8 | Launch SmoothZoom while native Magnifier (full-screen mode) is running: clear dialog appears. | AC-ERR.01 |
| E6.9 | Unplug a monitor while zoomed on it: no crash. Zoom transitions to the remaining monitor or adjusts viewport. | AC-ERR.05 |
| E6.10 | Trigger a UAC prompt while zoomed: no crash. Zoom resumes on return to the normal desktop. | AC-ERR.04 |
| E6.11 | Kill SmoothZoom via Task Manager while zoomed at 5.0×: crash handler resets zoom to 1.0×. Screen is not left magnified. | Crash recovery |
| E6.12 | All five performance targets are met on a machine with Intel UHD 620 graphics, 8GB RAM, Windows 10 22H2. | Scope §6.2 |
| E6.13 | All 139 acceptance criteria from Document 2 have been verified and pass. | Complete |

---

## 10. Phase Dependency Graph

```
Phase 0: Foundation and Risk Spike
   │
   │  Validates: Magnification API float precision,
   │  sub-frame latency, global hook scroll interception
   │
   ▼
Phase 1: Core Scroll-Gesture Zoom
   │
   │  Delivers: Scroll-gesture zoom, proportional viewport
   │  tracking, Start Menu suppression, MagBridge, RenderLoop
   │
   ▼
Phase 2: Keyboard Shortcuts and Animation
   │
   │  Adds: Win+Plus/Minus/Esc, ease-out animation engine,
   │  target retargeting, animation interruption by scroll
   │
   ▼
Phase 3: Accessibility Tracking
   │
   │  Adds: UIA thread, FocusMonitor, CaretMonitor,
   │  priority arbitration, source transitions, debounce
   │
   ▼
Phase 4: Temporary Toggle
   │
   │  Adds: Hold-to-peek with all state preservation
   │  edge cases (requires animation engine from P2
   │  and tracking system from P3 to test fully)
   │
   ▼
Phase 5: Settings, Tray, and Persistence
   │
   │  Adds: config.json, settings window, tray icon,
   │  configurable modifier/shortcuts, auto-start
   │  (requires all features to exist so all are configurable)
   │
   ▼
Phase 6: Polish, Multi-Monitor, and Hardening
   │
   │  Adds: Color inversion, multi-monitor, conflict detection,
   │  crash recovery, hook robustness, performance audit
   │
   ▼
Release Candidate
```

Phases 4 and 5 have a soft dependency: Phase 4 could theoretically ship before Phase 3 (the toggle doesn't strictly require focus/caret tracking), but testing the toggle's interaction with all three tracking sources is important, so Phase 3 comes first.

Phase 5 is sequenced last among the feature phases (before polish) because the settings UI must expose controls for every feature. Building the settings panel before the features exist would require rework.

---

## 11. Incremental Testing Strategy

Each phase has three layers of testing:

**Layer 1 — Automated unit tests.** Components with pure logic (ZoomController, ViewportTracker's offset math, WinKeyManager's state machine, SettingsManager's validation) are unit-tested in isolation with no dependency on the Magnification API or live hooks. These tests run in CI on any machine, no signing or secure folder required.

**Layer 2 — Manual integration tests.** Each phase's exit criteria table (Sections 3–9) serves as the manual test plan. These require a signed build running from a secure folder on a physical or virtual Windows machine. Each exit criterion is verified by a human tester.

**Layer 3 — Extended soak testing.** After each phase's exit criteria pass, the build runs as a daily-driver magnifier for an extended period (exact duration is a judgment call) to surface reliability issues: hook de-registration under load, memory leaks, viewport drift after hours of use, interaction with Windows Update, sleep/resume cycles, and screen lock transitions.

Specific testing notes by phase:

| Phase | Special Testing Considerations |
|-------|-------------------------------|
| 0 | Test on at least two machines with different GPUs (one Intel integrated, one discrete NVIDIA or AMD) to validate that float-precision zoom isn't driver-dependent. |
| 1 | Test on Precision Touchpad and legacy (non-precision) touchpad to verify scroll interception behavior differences. Test with both left- and right-Win keys. |
| 2 | Test rapid keyboard input (auto-repeat held down for 5+ seconds) to verify animation retargeting doesn't accumulate floating-point error. |
| 3 | Test the full application compatibility matrix (§6.3.5). This is the most labor-intensive manual testing phase. Budget accordingly. |
| 4 | Test toggle with deliberately adversarial timing: toggle engage/release in rapid alternation, toggle during scroll, toggle during keyboard animation, toggle during focus pan. |
| 5 | Test the full settings lifecycle: change every setting, verify immediate effect, exit, re-launch, verify persistence. Corrupt the config file in various ways (empty, invalid JSON, missing fields, values out of range). |
| 6 | Test on multi-monitor configurations: same resolution, different resolution, same DPI, different DPI, monitor connected/disconnected while zoomed. Test crash recovery: kill process via Task Manager at various zoom levels. |

---

## 12. Acceptance Criteria Coverage Map

Every acceptance criterion from Document 2, mapped to the phase where it is first testable:

| AC Range | Feature Area | Phase |
|----------|-------------|-------|
| AC-2.1.01 – AC-2.1.18 | Scroll-Gesture Zoom + Start Menu | 1 |
| AC-2.1.19 – AC-2.1.21 | Modifier Key Configuration | 5 |
| AC-2.2.01 – AC-2.2.03 | Scroll-Gesture Smoothness | 1 |
| AC-2.2.04 – AC-2.2.09 | Keyboard Animation | 2 |
| AC-2.2.10 | Pan Synchronization | 1 (scroll), 2 (keyboard) |
| AC-2.3.01 – AC-2.3.13 | Full-Screen Magnification | 1 |
| AC-2.4.01 – AC-2.4.13 | Continuous Viewport Tracking | 1 |
| AC-2.5.01 – AC-2.5.14 | Keyboard Focus Following | 3 |
| AC-2.6.01 – AC-2.6.11 | Text Cursor Following | 3 |
| AC-2.7.01 – AC-2.7.12 | Temporary Zoom Toggle | 4 |
| AC-2.8.01 – AC-2.8.10 | Keyboard Shortcuts | 2 |
| AC-2.8.11 | Settings Shortcut | 5 |
| AC-2.8.12 | Shortcut During Scroll | 2 |
| AC-2.9.01 – AC-2.9.19 | Settings and Configuration | 5 |
| AC-2.10.01 – AC-2.10.05 | Color Inversion | 6 |
| AC-MM.01 – AC-MM.04 | Multi-Monitor | 6 |
| AC-ERR.01 – AC-ERR.02 | Conflict Detection | 6 |
| AC-ERR.03 – AC-ERR.04 | Hook and Desktop Resilience | 1 (partial), 6 (full) |
| AC-ERR.05 | Display Config Changes | 6 |

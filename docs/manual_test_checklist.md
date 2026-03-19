# SmoothZoom Manual Test Checklist

**Prerequisites:** Signed binary, installed to secure folder (e.g., `C:\Program Files\SmoothZoom\`), UIAccess manifest, Windows 10 1903+ x64.

**Legend:** UT = covered by unit test, MAN = requires manual verification on signed build.

---

## Phase 6 Exit Criteria (E6.1–E6.13)

| ID | Test | Steps | Expected | Pass? |
|----|------|-------|----------|-------|
| E6.1 | Color inversion toggle | Press `Ctrl+Alt+I` while zoomed | Screen colors invert instantly. Press again: colors return. | [pass] |
| E6.2 | Color inversion persistence | Enable inversion, exit SmoothZoom, relaunch | Inversion is ON at launch | [fail but feature is unecessary] |
| E6.3 | Color inversion at 1.0x | Set zoom to 1.0x, press `Ctrl+Alt+I` | Screen inverts even though not magnified | [pass] |
| E6.4 | Multi-monitor: zoom follows pointer | Dual-monitor setup, zoom to 3x on monitor 1 | Zoom is active on monitor 1, monitor 2 at 1.0x | [pass] |
| E6.5 | Multi-monitor: pointer transfer | Move pointer from monitor 1 to monitor 2 while zoomed | Zoom transfers smoothly to monitor 2 | [pass] |
| E6.6 | Multi-monitor: DPI mismatch | Dual monitors at 100% and 150% DPI | Zoom is relative to each monitor's native content | [pass] |
| E6.7 | Multi-monitor: resolution mismatch | Dual monitors at different resolutions | Viewport tracking, deadzone, edge clamping correct on both | [ ] |
| E6.8 | Conflict detection | Launch SmoothZoom while native Magnifier (full-screen) is running | Clear dialog appears offering to disable or exit | [dialog appeared, other magnifer closed but scroll to zoom did not funciton after launching] |
| E6.9 | Display change while zoomed | Unplug a monitor while zoomed on it | No crash. Zoom transitions to remaining monitor | [ ] |
| E6.10 | Secure desktop (UAC) | Trigger UAC prompt while zoomed | No crash. Zoom resumes on return to normal desktop | [ ] |
| E6.11 | Crash recovery | Kill via Task Manager while zoomed at 5.0x | Crash handler resets zoom to 1.0x. Screen not left magnified | [pass] |
| E6.12 | Performance targets | See Performance section below | All 5 metrics met | [performance is mostly good but could be improved, logging for better visability?] |
| E6.13 | All 139 ACs verified | Complete this checklist | All pass | [ ] |

---

## Performance Metrics (E6.12)

Target hardware: Intel UHD 620 graphics, 8GB RAM, Windows 10 22H2 (or equivalent).

Enable `SMOOTHZOOM_PERF_AUDIT` build for QPC stats (visible in DebugView/VS Output).

| Metric | Procedure | Target | Measured | Pass? |
|--------|-----------|--------|----------|-------|
| CPU idle | Performance Monitor, zoom to 5x, pointer stationary, 60s | <2% | ____5q% | [ ] |
| CPU active | Same but pointer moving continuously, 60s | <5% | ____8% | [ ] |
| Memory | Task Manager working set during active zoom | <50MB | ____MB | [ ] |
| GPU | GPU-Z during active zoom at 5x | <10% (Intel UHD 620) | ____% | [ ] |
| Frame latency | QPC stats from PERF_AUDIT output | <=16ms at 60Hz | ____ms | [ ] |

---

## Acceptance Criteria by Feature Area

### 2.1 Scroll-Gesture Zoom (21 ACs)

| AC | Description | Type | Pass? |
|----|-------------|------|-------|
| AC-2.1.01 | Win+Scroll up zooms in, down zooms out | MAN | [pass] |
| AC-2.1.02 | Scroll without Win passes through to apps | MAN | [pass] |
| AC-2.1.03 | Releasing Win holds zoom level (no snap-back) | MAN | [pass] |
| AC-2.1.04 | Both LWin and RWin work as modifier | MAN | [unable to test on current keyboard] |
| AC-2.1.05 | Precision touchpad two-finger scroll zooms smoothly | UT+MAN | [pass] |
| AC-2.1.06 | Logarithmic model: 1x->2x same effort as 2x->4x | UT | [pass] |
| AC-2.1.07 | Zoom rate scales with scroll velocity | MAN | [pass] |
| AC-2.1.08 | Single notch produces ~10-20% visible change | MAN | [pass] |
| AC-2.1.09 | Zoom-in pulls point under pointer toward center | MAN | [pass] |
| AC-2.1.10 | Moving pointer during scroll uses current position as focal | MAN | [pass] |
| AC-2.1.11 | Pointer at center: symmetrical zoom, no drift | MAN | [there is occasionally some drift / viewport shift away from the pointer, logging could improve visability] |
| AC-2.1.12 | Pointer near corner: viewport stays within desktop bounds | MAN | [pass] |
| AC-2.1.13 | Zoom doesn't go below minimum (1.0x) | UT+MAN | [pass] |
| AC-2.1.14 | Zoom doesn't exceed maximum (10.0x) | UT+MAN | [pass] |
| AC-2.1.15 | Soft bounds decelerate near limits (no wall) | UT+MAN | [pass] |
| AC-2.1.16 | Win+Scroll then release Win: Start Menu suppressed | MAN | [mostly pass, somtimes still gets triggered - can we disable the windows to start menu shortcut completely when application is open?] |
| AC-2.1.17 | Win press+release without scroll: Start Menu opens | UT+MAN | [pass, reconsider behavior when smoothzoom is open] |
| AC-2.1.18 | Win+Scroll then Win+E: Explorer opens normally | MAN | [pass] |
| AC-2.1.19 | Changing modifier in settings takes effect immediately | MAN | [settings modifier changes don't appear to affect behavior of application] |
| AC-2.1.20 | Non-Win modifier: no Start Menu suppression logic | MAN | [fail] |
| AC-2.1.21 | Ctrl modifier consumes Ctrl+Scroll from apps | MAN | [fail, ctrl+scroll to zoom is unnecessary consider a different unused modifier] |

### 2.2 Smooth Zoom Animation (10 ACs)

| AC | Description | Type | Pass? |
|----|-------------|------|-------|
| AC-2.2.01 | Scroll zoom renders next frame (<=16ms at 60Hz) | MAN | [pass] |
| AC-2.2.02 | No visible frame drops during normal workloads | MAN | [pass] |
| AC-2.2.03 | Scroll zoom is direct/continuous, no lag-behind | MAN | [pass] |
| AC-2.2.04 | Keyboard zoom animates over 100-150ms | UT+MAN | [pass] |
| AC-2.2.05 | Ease-out timing: starts fast, decelerates | UT | [pass] |
| AC-2.2.06 | Rapid keyboard presses retarget smoothly | UT+MAN | [pass] |
| AC-2.2.07 | Plus then Minus reverses smoothly, no bounce | UT+MAN | [pass] |
| AC-2.2.08 | Scroll interrupts keyboard animation | UT+MAN | [pass] |
| AC-2.2.09 | Toggle interrupts ongoing animation | UT+MAN | [pass] |
| AC-2.2.10 | Viewport and zoom change as single coordinated motion | MAN | [pass] |

### 2.3 Full-Screen Magnification (13 ACs)

| AC | Description | Type | Pass? |
|----|-------------|------|-------|
| AC-2.3.01 | All screen content magnified uniformly | MAN | [pass] |
| AC-2.3.02 | No unmagnified "holes" in the view | MAN | [pass] |
| AC-2.3.03 | Mouse cursor visible and correctly positioned | MAN | [pass mostly] |
| AC-2.3.04 | Clicks interact with correct elements | MAN | [pass] |
| AC-2.3.05 | Drag operations work correctly while zoomed | MAN | [pass] |
| AC-2.3.06 | Hover states and tooltips at correct position | MAN | [pass] |
| AC-2.3.07 | Image smoothing ON: text edges anti-aliased | MAN (always on - API limitation) | [pass] |
| AC-2.3.08 | Image smoothing OFF: hard pixel edges | N/A (deferred to R-01) | [~] |
| AC-2.3.09 | Toggling smoothing changes next frame | N/A (deferred to R-01) | [~] |
| AC-2.3.10 | Magnified view at native refresh rate, no tearing | MAN | [pass] |
| AC-2.3.11 | Real-time content (video) updates smoothly | MAN | [pass] |
| AC-2.3.12 | At 1.0x: no visual artifact, overlay, or tint | MAN | [pass] |
| AC-2.3.13 | At 1.0x: no input latency or interception | MAN | [pass] |

### 2.4 Continuous Viewport Tracking (13 ACs)

| AC | Description | Type | Pass? |
|----|-------------|------|-------|
| AC-2.4.01 | Proportional mapping: pointer fraction = viewport fraction | UT+MAN | [ ] |
| AC-2.4.02 | Moving pointer right reveals more content right | MAN | [ ] |
| AC-2.4.03 | Continuous mapping, no discrete steps/zones | MAN | [ ] |
| AC-2.4.04 | Viewport never pans beyond desktop boundaries | UT+MAN | [ ] |
| AC-2.4.05 | All four corners reachable | UT+MAN | [ ] |
| AC-2.4.06 | Viewport movement visually smooth, no jumps | MAN | [ ] |
| AC-2.4.07 | No perceptible tracking lag | MAN | [ ] |
| AC-2.4.08 | Viewport stops immediately when pointer stops | MAN | [ ] |
| AC-2.4.09 | Micro-movements don't cause viewport jitter | MAN | [ ] |
| AC-2.4.10 | Deadzone not perceptible during intentional movement | MAN | [ ] |
| AC-2.4.11 | Deadzone scales with display resolution | MAN | [ ] |
| AC-2.4.12 | Tracking identical at all zoom levels 1.0x-10.0x | UT+MAN | [ ] |
| AC-2.4.13 | At 10.0x: tracking still smooth and controllable | MAN | [ ] |

### 2.5 Keyboard Focus Following (14 ACs)

| AC | Description | Type | Pass? |
|----|-------------|------|-------|
| AC-2.5.01 | Tab moves focus off-viewport: viewport pans to element | UT+MAN | [ ] |
| AC-2.5.02 | Shift+Tab moves backward: viewport pans | MAN | [ ] |
| AC-2.5.03 | Alt+Tab to new window: viewport pans | MAN | [ ] |
| AC-2.5.04 | Arrow keys in menu: viewport follows highlighted item | MAN | [ ] |
| AC-2.5.05 | Focus moves to already-visible element: no pan | MAN | [ ] |
| AC-2.5.06 | Partially visible element: pan to show with margin | MAN | [ ] |
| AC-2.5.07 | Rapid Tab: debounce 100ms, no chasing intermediates | UT+MAN | [ ] |
| AC-2.5.08 | Single Tab: pan starts promptly after debounce | UT+MAN | [ ] |
| AC-2.5.09 | After rapid tabbing stops: pan to final element | MAN | [ ] |
| AC-2.5.10 | Focus and pointer tracking both active simultaneously | UT+MAN | [ ] |
| AC-2.5.11 | Mouse after focus pan: pointer tracking resumes | UT+MAN | [ ] |
| AC-2.5.12 | Tab after mouse: viewport pans to focused element | UT+MAN | [ ] |
| AC-2.5.13 | Focus following works in Edge, Chrome, Firefox, Explorer, Notepad, etc. | MAN | [ ] |
| AC-2.5.14 | Apps without UIA focus: silent no-op | UT+MAN | [ ] |

### 2.6 Text Cursor Following (11 ACs)

| AC | Description | Type | Pass? |
|----|-------------|------|-------|
| AC-2.6.01 | Typing in text field: viewport keeps caret visible | MAN | [ ] |
| AC-2.6.02 | Arrow key navigation: viewport follows caret | MAN | [ ] |
| AC-2.6.03 | Home/End/PgUp/PgDn: smooth pan to new position | MAN | [ ] |
| AC-2.6.04 | Mouse click repositions caret: pan then handoff to pointer | MAN | [ ] |
| AC-2.6.05 | Find & Replace jumps caret: viewport pans | MAN | [ ] |
| AC-2.6.06 | Lookahead margin during active typing | UT+MAN | [ ] |
| AC-2.6.07 | Active typing: caret priority over pointer (500ms) | UT+MAN | [ ] |
| AC-2.6.08 | 500ms idle: pointer tracking resumes | UT+MAN | [ ] |
| AC-2.6.09 | Tab during typing: focus following takes over | UT+MAN | [ ] |
| AC-2.6.10 | Works in Notepad, Word, Chrome, VS Code, Terminal, etc. | MAN | [ ] |
| AC-2.6.11 | Apps without caret info: silent no-op | MAN | [ ] |

### 2.7 Temporary Zoom Toggle (12 ACs)

| AC | Description | Type | Pass? |
|----|-------------|------|-------|
| AC-2.7.01 | Zoomed + hold Ctrl+Alt: animate to 1.0x | UT+MAN | [ ] |
| AC-2.7.02 | While toggle held: stay at 1.0x, interact normally | MAN | [ ] |
| AC-2.7.03 | Release toggle: animate back to saved level | UT+MAN | [ ] |
| AC-2.7.04 | At 1.0x + hold toggle: animate to last-used zoom | UT+MAN | [ ] |
| AC-2.7.05 | First use (no history): animate to default (2.0x) | UT+MAN | [ ] |
| AC-2.7.06 | Release from 1.0x toggle: animate back to 1.0x | UT+MAN | [ ] |
| AC-2.7.07 | Brief tap (<100ms): full animation plays both ways | UT+MAN | [ ] |
| AC-2.7.08 | Toggle during scroll: captures current level | MAN | [ ] |
| AC-2.7.09 | Scroll during toggle: updates restore target | UT+MAN | [ ] |
| AC-2.7.10 | Toggle during animation: captures mid-animation level | UT+MAN | [ ] |
| AC-2.7.11 | Toggle key configurable, change takes effect immediately | MAN | [ ] |
| AC-2.7.12 | Toggle key can't conflict with modifier key | MAN | [ ] |

### 2.8 Keyboard Shortcuts (12 ACs)

| AC | Description | Type | Pass? |
|----|-------------|------|-------|
| AC-2.8.01 | Win+Plus increases zoom by step (default 25%) | UT+MAN | [ ] |
| AC-2.8.02 | Win+Minus decreases zoom by step | MAN | [ ] |
| AC-2.8.03 | Win+Plus at 9.9x clamps to 10.0x | UT+MAN | [ ] |
| AC-2.8.04 | Win+Minus at 1.1x clamps to 1.0x | UT+MAN | [ ] |
| AC-2.8.05 | Win+Plus at max: no effect | UT | [ ] |
| AC-2.8.06 | All keyboard zoom changes animate smoothly | UT+MAN | [ ] |
| AC-2.8.07 | Rapid Win+Plus: cumulative target, single animation | UT+MAN | [ ] |
| AC-2.8.08 | Win+Esc while zoomed: animate to 1.0x | UT+MAN | [ ] |
| AC-2.8.09 | Win+Esc at 1.0x: no effect | UT | [ ] |
| AC-2.8.10 | Win+Esc saves pre-escape level as last-used | MAN | [ ] |
| AC-2.8.11 | Win+Ctrl+M opens settings window | MAN | [ ] |
| AC-2.8.12 | Keyboard shortcut during Win+Scroll: step applied | MAN | [ ] |

### 2.9 Settings and Configuration (19 ACs)

| AC | Description | Type | Pass? |
|----|-------------|------|-------|
| AC-2.9.01 | Settings saved to JSON in %AppData%\SmoothZoom\ | UT+MAN | [ ] |
| AC-2.9.02 | Settings loaded on startup, defaults if missing | UT+MAN | [ ] |
| AC-2.9.03 | Corrupt config: starts with defaults, no crash | UT+MAN | [ ] |
| AC-2.9.04 | Changing modifier key takes effect immediately | UT+MAN | [ ] |
| AC-2.9.05 | Reduce max while above it: animate down | UT+MAN | [ ] |
| AC-2.9.06 | Raise min while below it: animate up | UT | [ ] |
| AC-2.9.07 | Toggle image smoothing: next frame | N/A (always on) | [~] |
| AC-2.9.08 | Toggle focus following off: stops immediately | MAN | [ ] |
| AC-2.9.09 | Toggle caret following off: stops immediately | MAN | [ ] |
| AC-2.9.10 | UI prevents min > max | UT+MAN | [ ] |
| AC-2.9.11 | UI prevents toggle key = modifier key conflict | MAN | [ ] |
| AC-2.9.12 | Keyboard step constrained 5-100% | UT+MAN | [ ] |
| AC-2.9.13 | System tray icon visible while running | MAN | [ ] |
| AC-2.9.14 | Right-click tray: Settings, Toggle, Exit | MAN | [ ] |
| AC-2.9.15 | Tray "Toggle Zoom": animate to 1.0x or last-used | UT+MAN | [ ] |
| AC-2.9.16 | Tray "Exit": animate to 1.0x then exit | MAN | [ ] |
| AC-2.9.17 | Start with Windows: launches at sign-in | MAN | [ ] |
| AC-2.9.18 | Start zoomed: applies default zoom at launch | MAN | [ ] |
| AC-2.9.19 | Start zoomed OFF: starts at 1.0x | MAN | [ ] |

### 2.10 Color Inversion (5 ACs)

| AC | Description | Type | Pass? |
|----|-------------|------|-------|
| AC-2.10.01 | Ctrl+Alt+I toggles inversion instantly | MAN | [ ] |
| AC-2.10.02 | Inversion applies to all content including cursor | MAN | [ ] |
| AC-2.10.03 | Toggle from settings UI: same effect | MAN | [ ] |
| AC-2.10.04 | Inversion state persists across sessions | MAN | [ ] |
| AC-2.10.05 | Inversion works at 1.0x | MAN | [ ] |

### Multi-Monitor Basics (4 ACs)

| AC | Description | Type | Pass? |
|----|-------------|------|-------|
| AC-MM.01 | Zoom on pointer's monitor, others at 1.0x | MAN | [ ] |
| AC-MM.02 | Pointer transfer: zoom follows smoothly | MAN | [ ] |
| AC-MM.03 | Different DPI: zoom relative to native content | MAN | [ ] |
| AC-MM.04 | Different resolutions: tracking adapts correctly | MAN | [ ] |

**Note:** AC-MM.01 through AC-MM.04 require multi-monitor hardware. `MagSetFullscreenTransform` magnifies the entire virtual desktop — per-monitor zoom may not be achievable with this API. Document actual behavior.

### Conflict and Error Handling (5 ACs)

| AC | Description | Type | Pass? |
|----|-------------|------|-------|
| AC-ERR.01 | Detect native Magnifier, show dialog | MAN | [ ] |
| AC-ERR.02 | API init failure: clear error message, graceful exit | MAN | [ ] |
| AC-ERR.03 | Hook deregistered: auto re-register + tray notification | MAN | [ ] |
| AC-ERR.04 | Secure desktop (UAC/lock): no crash, resume on return | MAN | [ ] |
| AC-ERR.05 | Display config change while zoomed: no crash, adapt | MAN | [ ] |

---

## Summary

| Category | Total ACs | Unit Tested | Manual Only | Deferred (API limitation) |
|----------|-----------|-------------|-------------|--------------------------|
| Scroll-Gesture Zoom | 21 | 5 | 16 | 0 |
| Smooth Zoom Animation | 10 | 6 | 4 | 0 |
| Full-Screen Magnification | 13 | 0 | 11 | 2 (AC-2.3.08, AC-2.3.09) |
| Viewport Tracking | 13 | 5 | 8 | 0 |
| Focus Following | 14 | 8 | 6 | 0 |
| Caret Following | 11 | 4 | 7 | 0 |
| Temporary Toggle | 12 | 8 | 4 | 0 |
| Keyboard Shortcuts | 12 | 7 | 5 | 0 |
| Settings | 19 | 10 | 8 | 1 (AC-2.9.07) |
| Color Inversion | 5 | 0 | 5 | 0 |
| Multi-Monitor | 4 | 0 | 4 | 0 |
| Error Handling | 5 | 0 | 5 | 0 |
| **Total** | **139** | **53** | **86** | **3** |

**3 ACs deferred** due to Magnification API limitation (image smoothing toggle requires Desktop Duplication API migration, tracked as R-01).

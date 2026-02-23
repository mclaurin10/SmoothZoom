# User-Facing Behavior Specification

## macOS-Style Full-Screen Zoom for Windows

### Document 2 of 5 — Development Plan Series

**Version:** 1.0
**Status:** Draft
**Last Updated:** February 2026
**Prerequisite:** Document 1 — Project Scope and Non-Scope (v1.1)

---

## 1. Purpose of This Document

This document translates every in-scope feature from the Project Scope (Document 1) into testable acceptance criteria. Each criterion is written so that a QA engineer can verify pass or fail without interpretation, and a developer can implement the behavior without guessing.

Acceptance criteria are numbered for traceability. The prefix maps to the Document 1 section that defines the feature (e.g., **AC-2.1.03** is the third criterion for Section 2.1, Scroll-Gesture Zoom).

Where a criterion involves timing, distances, or thresholds, the stated value is a tuning target. Values marked with **[tunable]** are expected to be adjusted during development based on feel-testing. Values without that marker are hard requirements.

---

## 2. Scroll-Gesture Zoom (Scope §2.1)

### 2A. Core Interaction

**AC-2.1.01** — While the `Win` key is held and the user scrolls the mouse wheel up (away from the user), the zoom level increases smoothly. Scrolling down decreases the zoom level smoothly.

**AC-2.1.02** — While the `Win` key is NOT held, all scroll events pass through to the foreground application with no modification, delay, or side effects. SmoothZoom is completely transparent to scroll input when the modifier is not engaged.

**AC-2.1.03** — When the user releases the `Win` key, the zoom level remains at whatever value it had reached. It does not snap back to 1.0× or to any other level.

**AC-2.1.04** — Both the left `Win` key (`VK_LWIN`) and the right `Win` key (`VK_RWIN`) function identically as the modifier.

**AC-2.1.05** — Two-finger vertical scroll on a Windows Precision Touchpad, while `Win` is held, produces the same zoom behavior as the mouse wheel. The continuous (non-notched) nature of touchpad scroll input produces correspondingly smooth, continuous zoom changes.

### 2B. Zoom Scaling Model

**AC-2.1.06** — Scroll-gesture zoom uses a logarithmic (multiplicative) scaling model. Each unit of scroll input multiplies or divides the current zoom level by a constant factor. This means zooming from 1.0× to 2.0× requires the same amount of scrolling as zooming from 2.0× to 4.0×, or from 5.0× to 10.0×. The zoom change feels perceptually even across the entire range.

**AC-2.1.07** — The zoom rate (how much the zoom level changes per unit of scroll input) scales with scroll velocity. Slow, deliberate scrolling produces fine-grained zoom adjustments. Fast scrolling produces rapid zoom changes. The mapping between scroll velocity and zoom rate should feel proportional and natural. **[tunable]**

**AC-2.1.08** — A single mouse wheel notch (typically 120 units of `WHEEL_DELTA`) at normal speed produces a small, clearly visible zoom change — approximately 10–20% of the current zoom level. **[tunable]**

### 2C. Zoom Center Point

**AC-2.1.09** — When the user scrolls to zoom in, the point on the desktop underneath the pointer moves toward the center of the screen. When the user scrolls to zoom out, it moves away from center. The pointer position is the zoom's focal point.

**AC-2.1.10** — If the user moves the pointer while scrolling, each successive scroll event uses the pointer's current position as its zoom center. The zoom center tracks the pointer in real time.

**AC-2.1.11** — When the pointer is at the exact center of the screen, zooming in and out produces a symmetrical expansion/contraction around the center with no lateral viewport drift.

**AC-2.1.12** — When the pointer is near a screen corner, zooming in centers on the pointer position as closely as possible while keeping the viewport within the bounds of the desktop. The viewport never pans beyond the desktop edges.

### 2D. Zoom Bounds

**AC-2.1.13** — The zoom level cannot go below the configured minimum (default: 1.0×). Scrolling down at the minimum level produces no change and no error.

**AC-2.1.14** — The zoom level cannot exceed the configured maximum (default: 10.0×). Scrolling up at the maximum level produces no change and no error.

**AC-2.1.15** — The zoom level transitions smoothly into the bounds — it does not "hit a wall." If the user is scrolling rapidly and the target would exceed the maximum, the zoom level decelerates smoothly to the limit rather than snapping to it.

### 2E. Start Menu Suppression

**AC-2.1.16** — If the user presses `Win`, scrolls (one or more notches in either direction), then releases `Win`, the Start Menu does NOT open.

**AC-2.1.17** — If the user presses `Win` and releases it WITHOUT scrolling, the Start Menu opens normally. SmoothZoom only suppresses the Start Menu when a scroll event was actually consumed during the `Win` key-down period.

**AC-2.1.18** — If the user presses `Win`, scrolls, then presses another key (e.g., `Win+E` to open Explorer) before releasing `Win`, the other `Win+key` shortcut executes normally. SmoothZoom does not block non-scroll `Win` key combinations.

### 2F. Modifier Key Configuration

**AC-2.1.19** — When the user changes the scroll-gesture modifier key in settings (e.g., from `Win` to `Ctrl`), the new modifier takes effect immediately. The old modifier ceases to activate zoom. No application restart is required.

**AC-2.1.20** — When a non-`Win` modifier is configured (e.g., `Ctrl`), Start Menu suppression logic is not active (it is not needed).

**AC-2.1.21** — When `Ctrl` is configured as the modifier, `Ctrl+Scroll` is consumed by SmoothZoom. Applications that normally respond to `Ctrl+Scroll` (e.g., browser zoom) will NOT receive those scroll events. This is a documented and expected trade-off, noted in the settings UI.

---

## 3. Smooth Zoom Animation (Scope §2.2)

### 3A. Scroll-Gesture Smoothness

**AC-2.2.01** — During scroll-gesture zoom, the zoom level change is rendered on the very next display frame after the scroll input is received. Target latency: ≤16ms at 60Hz, ≤8ms at 120Hz (i.e., within one frame of the display's refresh rate).

**AC-2.2.02** — During scroll-gesture zoom, there are no visible frame drops, stutters, or momentary freezes under normal desktop workloads (web browser, text editor, file explorer, and system tray active simultaneously).

**AC-2.2.03** — Scroll-gesture zoom is direct and continuous — the zoom level is the accumulated result of scroll input, not an animated transition to a target. There is no perceptible "lag behind" or "catch-up" effect.

### 3B. Keyboard-Shortcut Animation

**AC-2.2.04** — When a keyboard shortcut changes the zoom level (e.g., `Win+Plus`), the zoom animates from the current level to the target level over 100–150ms. **[tunable]**

**AC-2.2.05** — The keyboard-shortcut animation uses an ease-out timing curve: it starts at full speed and decelerates to a stop at the target level. The onset feels immediate; the arrival feels stable.

**AC-2.2.06** — If the user presses a zoom keyboard shortcut while a previous keyboard-shortcut animation is still in progress, the new target replaces the old one. The animation smoothly redirects toward the new target without pausing, reversing, or restarting. For example: pressing `Win+Plus` three times rapidly animates smoothly to the level that is three steps above the starting point, not three sequential start-stop animations.

**AC-2.2.07** — If the user presses `Win+Plus` and then immediately `Win+Minus` (opposing directions), the animation smoothly reverses toward the new target. There is no visual "bounce" or stutter at the reversal point.

### 3C. Animation Interruption

**AC-2.2.08** — If the user begins a scroll-gesture zoom while a keyboard-shortcut animation is in progress, the scroll gesture takes over immediately. The animation stops, and the zoom level responds directly to scroll input from that point.

**AC-2.2.09** — If the user activates the temporary zoom toggle (§2.7) while any other zoom animation is in progress, the toggle takes over. On release, the zoom returns to whatever level the toggle interrupted — not to the pre-animation level.

### 3D. Viewport Pan Synchronization

**AC-2.2.10** — During any zoom level change (scroll-gesture, keyboard shortcut, or temporary toggle), the viewport position and the zoom level change as a single coordinated motion. The viewport pans toward the zoom center point at the same rate that the zoom level changes. There is never a moment where the zoom level has changed but the viewport has not yet caught up, or vice versa.

---

## 4. Full-Screen Magnification (Scope §2.3)

### 4A. Content Coverage

**AC-2.3.01** — At any zoom level above 1.0×, ALL visible screen content is magnified uniformly. This includes: application windows, the desktop, the taskbar, system tray icons, notification toasts, context menus (right-click menus), dropdown menus, tooltips, dialog boxes, the on-screen keyboard, and any third-party overlays rendered through standard Windows compositing.

**AC-2.3.02** — There are no "holes" in the magnified view — no region of the screen displays unmagnified content while the rest is magnified.

### 4B. Cursor Behavior

**AC-2.3.03** — The mouse cursor is visible at all times while zoomed. It renders at its correct position within the magnified view.

**AC-2.3.04** — Clicking on any screen element while zoomed interacts with the correct element. If a button is visible at the pointer's position in the magnified view, clicking activates that button.

**AC-2.3.05** — Drag operations work correctly while zoomed. Dragging a window title bar moves the window. Dragging a scrollbar scrolls the content. Dragging a file onto a folder moves the file. The drag tracks the pointer position accurately.

**AC-2.3.06** — Hover states work correctly while zoomed. Hovering over a button highlights it. Hovering over a hyperlink changes the cursor to a hand. Tooltips appear at the correct position.

### 4C. Image Smoothing

**AC-2.3.07** — With image smoothing ON (the default): magnified text edges appear smooth. At 4× zoom, individual characters are large but their edges are anti-aliased, not pixelated.

**AC-2.3.08** — With image smoothing OFF: magnified content shows hard pixel edges. At 4× zoom, individual screen pixels are visible as sharp squares. This is useful for users doing pixel-level inspection work.

**AC-2.3.09** — Toggling image smoothing in settings changes the rendering on the next display frame. No restart or re-zoom is required.

### 4D. Rendering Quality

**AC-2.3.10** — The magnified view renders at the display's native refresh rate (e.g., 60fps, 120fps, 144fps) with no visible tearing.

**AC-2.3.11** — Content that updates in real time (video playback, animations, screen recording previews) continues to update smoothly within the magnified view.

### 4E. Zoom Level 1.0× Behavior

**AC-2.3.12** — At zoom level 1.0×, the screen appears completely normal. There is no visual artifact, overlay, tint, or performance impact. SmoothZoom is perceptually invisible when not zoomed.

**AC-2.3.13** — At zoom level 1.0×, all mouse clicks, keyboard input, and application interactions behave identically to when SmoothZoom is not running. There is no input latency added, no event interception (other than the modifier key watch), and no coordinate transformation.

---

## 5. Continuous Viewport Tracking (Scope §2.4)

### 5A. Proportional Mapping

**AC-2.4.01** — The viewport position is determined by a proportional mapping between the pointer's physical screen position and the desktop area. Conceptually: the pointer's position as a fraction of the screen dimensions maps to the viewport's position as a fraction of the pannable desktop area. When the pointer is at the top-left corner of the physical screen, the viewport shows the top-left corner of the desktop. When the pointer is at the bottom-right, the viewport shows the bottom-right. When the pointer is at the center, the viewport is centered.

**AC-2.4.02** — Moving the pointer to the right always reveals more desktop content to the right (and vice versa for all four directions). There is never a situation where moving the pointer in one direction causes the viewport to move in the opposite direction.

**AC-2.4.03** — The proportional mapping is continuous. Moving the pointer smoothly across the screen produces a smooth, continuous viewport pan. There are no discrete "steps" or "zones" in the tracking.

### 5B. Edge and Corner Behavior

**AC-2.4.04** — The viewport never pans beyond the boundaries of the desktop. When the pointer is at any edge of the screen, the viewport shows the corresponding edge of the desktop with no blank/black space visible.

**AC-2.4.05** — When the pointer is in a corner of the screen, the viewport shows the corresponding corner of the desktop. All four corners are reachable.

### 5C. Smoothness and Responsiveness

**AC-2.4.06** — Viewport movement is visually smooth. There are no visible jumps, snaps, or discontinuities during normal pointer movement.

**AC-2.4.07** — Viewport tracking has no perceptible lag. When the pointer moves, the viewport responds on the same display frame. There is no "trailing" effect where the viewport catches up after the pointer stops.

**AC-2.4.08** — When the pointer stops moving, the viewport stops immediately. There is no momentum, drift, or oscillation after the pointer comes to rest.

### 5D. Deadzone

**AC-2.4.09** — When the pointer is nearly stationary (micro-movements of 2–5 pixels at 1080p resolution), the viewport does not jitter. A small deadzone prevents imperceptible pointer movements from producing visible viewport motion. **[tunable]**

**AC-2.4.10** — The deadzone is not perceptible during intentional pointer movement. When the user deliberately moves the pointer, even slowly, the viewport responds without any sensation of "stickiness" or delay.

**AC-2.4.11** — The deadzone threshold scales proportionally with display resolution. A 3-pixel deadzone at 1920×1080 corresponds to approximately 6 pixels at 3840×2160.

### 5E. Tracking Across Zoom Levels

**AC-2.4.12** — Continuous tracking operates identically at all zoom levels from just above 1.0× to the maximum (10.0×). The proportional mapping adjusts automatically: at higher zoom, smaller pointer movements produce proportionally larger viewport shifts (because the viewport covers less of the desktop).

**AC-2.4.13** — At very high zoom levels (e.g., 10.0×), the tracking remains smooth and controllable. The pointer sensitivity does not become so high that the viewport is difficult to position precisely.

---

## 6. Keyboard Focus Following (Scope §2.5)

### 6A. Core Behavior

**AC-2.5.01** — When the user presses `Tab` and focus moves to a UI element that is outside the current viewport, the viewport smoothly pans to bring the focused element into view.

**AC-2.5.02** — When the user presses `Shift+Tab` and focus moves backward to a UI element that is outside the current viewport, the viewport smoothly pans to that element.

**AC-2.5.03** — When the user presses `Alt+Tab` to switch windows, the viewport smoothly pans to show the newly focused window.

**AC-2.5.04** — When the user navigates a menu using arrow keys (up/down in a dropdown, left/right across a menu bar), the viewport pans to follow the highlighted item if it moves outside the current viewport.

**AC-2.5.05** — When focus moves to an element that is already fully visible within the current viewport, no panning occurs. The viewport remains still.

**AC-2.5.06** — When focus moves to an element that is partially visible (clipped by the viewport edge), the viewport pans enough to bring the entire element into view with a small margin around it. **[tunable — margin target: 30–50 pixels at 1080p]**

### 6B. Debounce

**AC-2.5.07** — When the user holds `Tab` and focus cycles rapidly through many controls, the viewport does not chase every intermediate focus change. Instead, the viewport waits approximately 100ms after the most recent focus change before panning. **[tunable]**

**AC-2.5.08** — A single `Tab` press (focus moves once) triggers a viewport pan after the debounce window (100ms) with no further focus change. The pan begins promptly — the debounce does not add perceptible delay to a single navigation action.

**AC-2.5.09** — When rapid tabbing stops, the viewport pans smoothly to the final focused element. It does not pan to any intermediate element that was focused during the rapid cycling.

### 6C. Interaction with Pointer Tracking

**AC-2.5.10** — Focus-following and pointer-tracking are both active simultaneously. They cooperate, not compete. The most recent input source determines the viewport position.

**AC-2.5.11** — If focus moves the viewport to a new location and the user then moves the mouse, pointer tracking resumes immediately. The viewport smoothly transitions from the focus-following position to the pointer-tracking position.

**AC-2.5.12** — If the user moves the mouse and then presses `Tab` (moving focus to an off-viewport element), the viewport pans to the focused element, temporarily overriding pointer tracking.

### 6D. UIA Compatibility

**AC-2.5.13** — Focus following works in applications that implement Windows UI Automation focus events. Verified in: Microsoft Edge, Google Chrome, Mozilla Firefox, File Explorer, Windows Settings, Notepad, Microsoft Word, Microsoft Excel, Visual Studio Code, and Windows system dialogs (Open, Save, Print).

**AC-2.5.14** — In applications that do not implement UIA focus events (e.g., some games, custom-drawn legacy applications), focus following silently does nothing. There is no error, no crash, and no degraded behavior in other features. Pointer tracking continues to work normally.

---

## 7. Text Cursor Following (Scope §2.6)

### 7A. Core Behavior

**AC-2.6.01** — While the user is typing in a text field, text editor, or document, the viewport keeps the text cursor (caret) visible at all times.

**AC-2.6.02** — When the caret moves due to arrow key navigation (`Left`, `Right`, `Up`, `Down`), the viewport pans to follow if the caret moves outside the current viewport.

**AC-2.6.03** — When the caret jumps due to `Home`, `End`, `Ctrl+Home`, `Ctrl+End`, `Page Up`, or `Page Down`, the viewport pans smoothly to the new caret position.

**AC-2.6.04** — When the caret moves due to a mouse click that repositions it within a text field, the viewport pans to show the new caret position, then transitions to pointer tracking after the typing idle timeout (see AC-2.6.07).

**AC-2.6.05** — When the caret jumps due to a programmatic action (e.g., Find & Replace navigating to the next match), the viewport pans to show the new caret position.

### 7B. Lookahead Margin

**AC-2.6.06** — While the user is typing and the caret is advancing (e.g., left-to-right in English), the viewport maintains a lookahead margin ahead of the caret in the typing direction. The caret never sits at the very edge of the viewport during active typing. The margin provides enough visible space ahead for the user to see upcoming text or the end of the line. **[tunable — target: approximately 15–20% of the viewport width]**

### 7C. Priority and Handoff

**AC-2.6.07** — While the user is actively typing (keyboard input within the last 500ms), caret following takes priority over pointer following. Mouse movements during typing do not yank the viewport away from the caret. **[tunable — idle threshold: 500ms]**

**AC-2.6.08** — After 500ms of no keyboard input, pointer following resumes as the primary tracking source. The transition is smooth — the viewport glides from the caret-following position to the pointer-tracking position.

**AC-2.6.09** — If the user is typing and presses `Tab` (moving focus to another control), focus following (§2.5) takes over from caret following. If the user then begins typing in the new control, caret following resumes in the new context.

### 7D. Compatibility

**AC-2.6.10** — Caret following works in: Notepad, WordPad, Microsoft Word, Microsoft Excel (formula bar and cell editing), Google Chrome (address bar and web page text inputs), Mozilla Firefox (same), Microsoft Edge (same), Visual Studio Code, Windows Terminal, and Windows Search.

**AC-2.6.11** — In applications where the caret position cannot be determined (e.g., some games, remote desktop clients rendering a remote cursor), caret following silently does nothing. Other features continue to work normally.

---

## 8. Temporary Zoom Toggle (Scope §2.7)

### 8A. State: Zoomed In → Peek at 1.0×

**AC-2.7.01** — When the zoom level is above 1.0× and the user presses and holds the temporary toggle key combination (default: `Ctrl+Alt`), the zoom level smoothly animates to 1.0× over 200–300ms. **[tunable]**

**AC-2.7.02** — While the toggle keys remain held, the screen stays at 1.0×. The user can move the mouse, click, interact with the desktop — all at normal (unmagnified) view.

**AC-2.7.03** — When the user releases the toggle keys, the zoom level smoothly animates back to the level it was at before the toggle was activated. The animation uses the same duration (200–300ms).

### 8B. State: At 1.0× → Peek at Zoomed

**AC-2.7.04** — When the zoom level is 1.0× and the user presses and holds the toggle keys, the zoom level smoothly animates to the last-used zoom level.

**AC-2.7.05** — If there is no last-used zoom level (first use in the session), the toggle animates to the configured default zoom level (default: 2.0×).

**AC-2.7.06** — When the user releases the toggle keys, the zoom level smoothly animates back to 1.0×.

### 8C. Edge Cases

**AC-2.7.07** — If the user taps the toggle keys very briefly (press and release in under 100ms), the full animation sequence still plays: animate to the toggled state, then immediately begin animating back. The result is a brief "flash" of the alternate view. There is no minimum hold duration.

**AC-2.7.08** — If the user activates the toggle during an in-progress scroll-gesture zoom, the toggle takes effect from whatever zoom level has been reached. On release, the zoom returns to that same level (not to the level before the scroll gesture began).

**AC-2.7.09** — If the user scrolls (with modifier key held) while the toggle is also held, the scroll events are processed normally — they change the zoom level even during the toggle hold. On toggle release, the zoom returns to the new scroll-established level.

**AC-2.7.10** — If the user activates the toggle during an in-progress keyboard-shortcut animation, the animation is interrupted and the toggle takes effect from the current (mid-animation) zoom level. On release, the zoom returns to that mid-animation level.

### 8D. Configuration

**AC-2.7.11** — The toggle key combination is configurable in settings. Changing it takes effect immediately.

**AC-2.7.12** — The toggle key combination must differ from the scroll-gesture modifier key. If the user attempts to set them to a conflicting combination, the settings UI prevents this and explains why.

---

## 9. Keyboard Shortcuts (Scope §2.8)

### 9A. Zoom In / Zoom Out

**AC-2.8.01** — Pressing `Win+Plus` increases the zoom level by the configured step size (default: 25%). The step is additive: at 2.0×, a 25% step targets 2.25×. At 4.0×, it targets 4.25×.

**AC-2.8.02** — Pressing `Win+Minus` decreases the zoom level by the configured step size. At 2.0×, a 25% step targets 1.75×. At 1.25×, it targets 1.0×.

**AC-2.8.03** — If a zoom-in step would exceed the configured maximum (10.0×), the zoom level animates to exactly the maximum. For example: at 9.90×, pressing `Win+Plus` with a 25% step targets 10.15×, which is clamped to 10.0×.

**AC-2.8.04** — If a zoom-out step would go below the configured minimum (1.0×), the zoom level animates to exactly the minimum.

**AC-2.8.05** — Pressing `Win+Plus` at the maximum zoom level produces no effect. Pressing `Win+Minus` at the minimum produces no effect. No error, no sound, no visual feedback beyond the zoom level not changing.

**AC-2.8.06** — All keyboard-shortcut zoom changes animate smoothly per §3 (Smooth Zoom Animation). They do not snap.

**AC-2.8.07** — Pressing `Win+Plus` multiple times rapidly (e.g., three times within 300ms) smoothly animates to the cumulative target (three steps above the starting point), not three sequential animations.

### 9B. Toggle Off

**AC-2.8.08** — Pressing `Win+Esc` while zoomed (above 1.0×) smoothly animates the zoom level to 1.0×.

**AC-2.8.09** — Pressing `Win+Esc` while already at 1.0× has no effect.

**AC-2.8.10** — After pressing `Win+Esc` to zoom out, the temporary toggle (§2.7) remembers the pre-escape zoom level as the "last-used" level.

### 9C. Open Settings

**AC-2.8.11** — Pressing `Win+Ctrl+M` opens the SmoothZoom settings window. If the settings window is already open, it is brought to the foreground.

### 9D. Interaction with Scroll-Gesture

**AC-2.8.12** — If the user presses a keyboard zoom shortcut while the `Win` key is held for scroll-gesture zoom (e.g., holding `Win`, scrolling, then pressing `Plus`), the keyboard shortcut's step is applied as an animated jump on top of the current scroll-gesture position. This is an uncommon interaction and does not need to be perfectly fluid — it simply must not crash or produce an incorrect zoom level.

---

## 10. Settings and Configuration (Scope §2.9)

### 10A. Persistence

**AC-2.9.01** — All settings are saved to a local configuration file in `%AppData%\SmoothZoom\` (or equivalent user-writable location). The file is human-readable (JSON format).

**AC-2.9.02** — On application startup, settings are loaded from the configuration file. If the file does not exist, defaults are used and the file is created on the first settings change or on application exit.

**AC-2.9.03** — If the configuration file is corrupted or contains invalid values, the application starts with default settings and logs a warning. It does not crash.

### 10B. Immediate Effect

**AC-2.9.04** — Changing the scroll-gesture modifier key takes effect immediately. The very next scroll event respects the new modifier.

**AC-2.9.05** — Changing the maximum zoom level while currently zoomed above the new maximum causes the zoom level to smoothly animate down to the new maximum.

**AC-2.9.06** — Changing the minimum zoom level while currently zoomed below the new minimum causes the zoom level to smoothly animate up to the new minimum. (This scenario only arises if the minimum is raised above 1.0×.)

**AC-2.9.07** — Toggling image smoothing on or off takes effect on the next display frame.

**AC-2.9.08** — Toggling keyboard focus following off stops viewport panning in response to focus changes immediately. Other tracking (pointer, caret) is unaffected.

**AC-2.9.09** — Toggling text cursor following off stops viewport panning in response to caret changes immediately. Other tracking (pointer, focus) is unaffected.

### 10C. Validation

**AC-2.9.10** — The settings UI prevents the user from setting a minimum zoom level that exceeds the maximum zoom level.

**AC-2.9.11** — The settings UI prevents the user from setting the temporary toggle key combination to a value that conflicts with the scroll-gesture modifier key. An inline message explains the conflict.

**AC-2.9.12** — The keyboard shortcut zoom step is constrained to the range 5%–100%. Values outside this range cannot be entered.

### 10D. System Tray

**AC-2.9.13** — SmoothZoom displays an icon in the system tray (notification area) at all times while running.

**AC-2.9.14** — Right-clicking the tray icon shows a context menu with: "Settings", "Toggle Zoom On/Off", and "Exit".

**AC-2.9.15** — Selecting "Toggle Zoom On/Off" from the tray menu animates the zoom to 1.0× if zoomed, or to the last-used / default zoom level if at 1.0×.

**AC-2.9.16** — Selecting "Exit" closes SmoothZoom. If the screen is currently zoomed, it first smoothly animates to 1.0×, then exits. The application does not leave the screen in a magnified state on exit.

### 10E. Startup

**AC-2.9.17** — When "Start with Windows" is ON, SmoothZoom launches automatically at user sign-in (via Start menu Startup folder, Registry `Run` key, or Task Scheduler — implementation's choice). It starts in the background with the system tray icon visible.

**AC-2.9.18** — When "Start zoomed" is ON, SmoothZoom applies the configured default zoom level immediately on launch. The zoom animation plays after the desktop is visible.

**AC-2.9.19** — When "Start zoomed" is OFF (the default), SmoothZoom starts at 1.0× and waits for user input to engage zoom.

---

## 11. Color Inversion (Scope §2.10)

**AC-2.10.01** — Pressing `Ctrl+Alt+I` toggles color inversion on the magnified view. White becomes black, black becomes white, and all colors are inverted. The toggle is instantaneous (no animation).

**AC-2.10.02** — Color inversion applies to all magnified content including the cursor. The inversion is a property of the magnified output, not a system-wide display setting.

**AC-2.10.03** — Color inversion can be toggled from the settings UI with the same immediate effect.

**AC-2.10.04** — Color inversion state persists across sessions. If the user exits SmoothZoom with inversion ON, it is ON when the application next launches.

**AC-2.10.05** — Color inversion works at all zoom levels, including 1.0×. (At 1.0× with inversion ON, the screen appears color-inverted even though it is not magnified.)

---

## 12. Multi-Monitor Basics

Although advanced multi-monitor features are out of scope, the following basic behaviors are required for SmoothZoom to be usable on multi-monitor systems.

**AC-MM.01** — On a multi-monitor system, SmoothZoom magnifies the monitor where the pointer currently resides. The other monitor(s) display their content at normal (1.0×) zoom.

**AC-MM.02** — When the user moves the pointer from one monitor to another, zoom follows the pointer to the new monitor. The transition should be smooth — the old monitor returns to 1.0× and the new monitor zooms to the current zoom level.

**AC-MM.03** — Monitors with different DPI scaling settings (e.g., one at 100%, another at 150%) are handled correctly. The zoom level on each monitor is relative to that monitor's native content, not its DPI setting. A 2.0× zoom on a 150% DPI monitor makes content appear 2× larger than it normally does on that monitor.

**AC-MM.04** — Monitors with different resolutions (e.g., one 1920×1080, another 2560×1440) function correctly. Viewport tracking, zoom bounds, and deadzone thresholds adjust to the active monitor's resolution.

---

## 13. Conflict and Error Handling

**AC-ERR.01** — If SmoothZoom launches and detects that the native Windows Magnifier is already running in full-screen mode, it displays a clear message explaining that only one full-screen magnifier can be active at a time. It offers to disable the native Magnifier or to exit. SmoothZoom does not crash or silently fail.

**AC-ERR.02** — If the Magnification API fails to initialize (e.g., on an unsupported OS version, or if the application is not properly signed and installed), SmoothZoom displays a clear error message explaining the issue and exits gracefully.

**AC-ERR.03** — If the global input hook is unregistered by the system (e.g., due to a timeout), SmoothZoom detects this and re-registers the hook automatically. If re-registration fails, it logs a warning and displays a tray notification. Other functionality (keyboard shortcuts, settings) continues to work.

**AC-ERR.04** — When the system enters a state where magnification is unavailable (secure desktop, UAC prompt), SmoothZoom does not crash or display errors. When the system returns to the normal desktop, magnification resumes at its previous state.

**AC-ERR.05** — If the user's display configuration changes while zoomed (monitor connected/disconnected, resolution change, DPI change), SmoothZoom adapts without crashing. If the current zoom level or viewport position is no longer valid for the new configuration, the zoom level is preserved and the viewport is repositioned to a valid location.

---

## 14. Tracking Priority Summary

When multiple tracking sources (pointer, focus, caret) compete, the following priority rules apply. These are consolidated here for easy reference; the individual rules appear in their respective sections above.

| Situation | Active Tracking Source | Rule |
|-----------|----------------------|------|
| User is moving the mouse (no keyboard activity) | Pointer | Default state. Continuous proportional mapping. |
| User presses Tab / keyboard navigation | Focus | Viewport pans to focused element. Pointer tracking paused until mouse moves. (AC-2.5.10–12) |
| User is actively typing (keyboard input within 500ms) | Caret | Viewport follows text cursor. Pointer tracking paused until 500ms idle. (AC-2.6.07–08) |
| User types Tab while in a text field | Focus | Focus following takes over from caret following. (AC-2.6.09) |
| User moves mouse after a focus/caret pan | Pointer | Pointer tracking resumes immediately, transitioning smoothly. (AC-2.5.11, AC-2.6.08) |
| Temporary toggle is active | Pointer | At 1.0× (or peeked zoom), pointer tracking operates normally in the toggled view. |
| Scroll-gesture zoom in progress | Pointer + Zoom center | Viewport tracks both the zoom center point and the pointer's proportional position. (AC-2.2.10) |

---

## 15. Acceptance Criteria Index

For quick reference, all acceptance criteria organized by ID prefix:

| Prefix | Feature Area | Count |
|--------|-------------|-------|
| AC-2.1 | Scroll-Gesture Zoom | 21 |
| AC-2.2 | Smooth Zoom Animation | 10 |
| AC-2.3 | Full-Screen Magnification | 13 |
| AC-2.4 | Continuous Viewport Tracking | 13 |
| AC-2.5 | Keyboard Focus Following | 14 |
| AC-2.6 | Text Cursor Following | 11 |
| AC-2.7 | Temporary Zoom Toggle | 12 |
| AC-2.8 | Keyboard Shortcuts | 12 |
| AC-2.9 | Settings and Configuration | 19 |
| AC-2.10 | Color Inversion | 5 |
| AC-MM | Multi-Monitor Basics | 4 |
| AC-ERR | Conflict and Error Handling | 5 |
| **Total** | | **139** |

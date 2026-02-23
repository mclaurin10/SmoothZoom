# Project Scope and Non-Scope

## macOS-Style Full-Screen Zoom for Windows

### Document 1 of 5 — Development Plan Series

**Version:** 1.1
**Status:** Draft
**Last Updated:** February 2026

---

## 1. Project Definition

### 1.1 Purpose

This project delivers a Windows application — working title **SmoothZoom** — that provides full-screen magnification modeled on the behavior of macOS Accessibility Zoom. Specifically, it replicates two capabilities that the native Windows Magnifier lacks entirely: smooth scroll-gesture-driven zoom control, and fluid continuous viewport tracking that follows the user's pointer, keyboard focus, and text cursor without jarring jumps or stepped transitions.

### 1.2 Problem Statement

The native Windows Magnifier's full-screen mode has three fundamental shortcomings:

**No scroll-gesture activation.** Changing the zoom level requires keyboard shortcuts (`Win + Plus / Minus`) or clicking toolbar buttons. There is no way to hold a modifier key and scroll to zoom. This forces an unnatural two-handed interaction and prevents the kind of fine-grained, one-handed zoom control that macOS users take for granted.

**Stepped, instantaneous zoom.** Zoom level snaps between discrete increments (minimum 25% per step) with no animation. This is visually jarring and spatially disorienting, particularly for users with vestibular sensitivities who depend on smooth visual transitions to maintain spatial awareness of where they are on the desktop.

**Stiff viewport movement.** While zoomed in, the magnified view tracks the pointer, but movement feels mechanical — the viewport either locks the pointer to center or waits until the pointer hits an edge before jumping. There is no mode where the viewport glides proportionally with the pointer in real time, which is the default and most natural-feeling behavior on macOS.

### 1.3 One-Sentence Scope Statement

SmoothZoom is a full-screen magnifier for Windows that lets the user hold the `Win` key and scroll to smoothly zoom in and out (up to 10×), with a viewport that continuously follows the pointer, keyboard focus, and text cursor.

---

## 2. In-Scope Features

Each feature is described from the user's perspective. These descriptions are the starting points for formal acceptance criteria, which will be defined in the Behavior Specification document (Document 2).

### 2.1 Scroll-Gesture Zoom

The user holds the `Win` key and scrolls the mouse wheel or performs a two-finger vertical swipe on a precision touchpad. The screen zooms in or out smoothly, centered on the pointer's current position. Faster scrolling produces faster zoom changes. Releasing the `Win` key stops zoom changes but the screen remains at whatever zoom level it has reached. When `Win` is not held, scroll events pass through to applications normally with no interference.

**Key behavioral details:**

- The default modifier key is `Win` (the Windows logo key, virtual key codes `VK_LWIN` / `VK_RWIN`). Alternative options configurable in settings: `Ctrl`, `Alt`, `Shift`, and two-key combinations of these.
- `Win+Scroll` has no known conflicts with any mainstream Windows application or built-in OS behavior, making it an ideal default.
- **Start Menu suppression:** Pressing and releasing the `Win` key by itself normally opens the Start Menu. When SmoothZoom detects that a scroll event was consumed while `Win` was held, it must suppress the Start Menu activation on key release. This is a well-established technique used by tools such as PowerToys and AutoHotkey, typically accomplished by injecting a no-op keypress (e.g., a phantom `VK_NONAME` or `Ctrl` down/up) before the `Win` key-up event reaches the shell.
- Scroll events are consumed (not forwarded to applications) only while the modifier key is held.
- The zoom center point is the pointer's screen position at the moment each scroll event arrives. If the user moves the pointer while scrolling, the zoom center tracks with it.
- Zoom level is bounded by a configurable minimum (default: 1.0×, i.e., normal size) and maximum (default: 10.0×). The user cannot scroll past either bound.
- Precision touchpad continuous scroll input produces correspondingly continuous zoom changes — not quantized steps.

### 2.2 Smooth Zoom Animation

All zoom level changes animate fluidly. The screen never jumps between zoom levels. This applies to scroll-gesture-initiated zoom, keyboard-shortcut-initiated zoom, and any programmatic zoom changes (e.g., temporary toggle).

**Key behavioral details:**

- Scroll-gesture zoom is continuous and real time. The zoom level tracks the scroll input with no more than one display frame of latency (target: ≤16ms at 60Hz).
- Keyboard-shortcut zoom (if keyboard shortcuts are provided as a secondary activation method) animates from the current level to the target level using ease-out timing. Target animation duration: 100–150ms — fast enough to feel responsive, slow enough to be visually smooth.
- If a new zoom change begins while a previous animation is still in progress, the new target replaces the old one seamlessly. There is no queuing, stuttering, or visual reversal.
- Viewport panning (the movement of the visible area toward the zoom center point) animates in sync with the zoom level change. Zoom and pan are a single coordinated motion, not two sequential ones.

### 2.3 Full-Screen Magnification

When zoom is active (level > 1.0×), the entire screen is uniformly magnified. The user sees a portion of the desktop at the configured magnification. All windows, the taskbar, system tray, context menus, tooltips, notification toasts, overlays, and desktop icons are magnified. The mouse cursor remains visible and correctly positioned — clicks, drags, and hovers interact with the correct underlying screen elements regardless of zoom level.

**Key behavioral details:**

- Image smoothing (bilinear filtering / anti-aliasing) is applied to the magnified content by default. The user can disable this in settings if they prefer nearest-neighbor (sharp pixel) rendering.
- The magnified view must be rendered at the display's native refresh rate with no visible tearing or frame drops under normal desktop workloads.
- This is the only zoom mode the application provides. There is no lens, docked panel, picture-in-picture, or split-screen mode.

### 2.4 Continuous Viewport Tracking

While zoomed in, the visible portion of the screen moves proportionally as the pointer moves. Moving the pointer a small amount produces a small viewport shift. Moving it a large amount produces a large shift. The pointer can be anywhere on the visible screen — it is not locked to the center and does not need to reach an edge before the viewport begins to move. The effect is that the viewport glides with the pointer in real time.

This is the only pointer-tracking mode the application provides.

**Key behavioral details:**

- The tracking uses a proportional mapping: the pointer's position within the physical screen maps to a corresponding position within the virtual (unmagnified) desktop. As the pointer moves across the physical screen, the viewport scrolls to reveal the corresponding area of the desktop. This produces the natural "the screen moves with me" feeling.
- A small deadzone around the pointer's current position prevents micro-jitter when the pointer is nearly at rest. The deadzone should be small enough to be imperceptible during intentional movement (tuning target: 2–5 pixels at 1080p, scaled proportionally at higher resolutions).
- Viewport motion must be smooth — no snapping, no stepping. If the pointer moves rapidly, the viewport should keep up without lag, but should not overshoot and oscillate.

### 2.5 Keyboard Focus Following

When the user performs a keyboard action that moves focus to a different UI element — pressing `Tab`, `Shift+Tab`, arrow keys in a menu, `Alt+Tab` to switch windows, `Alt` to enter a menu bar, or any similar navigation — the magnified viewport smoothly pans to bring the newly focused element into view.

**Key behavioral details:**

- Focus following is active simultaneously with pointer following. If focus moves to a control that is off-screen in the current viewport, the viewport pans to it. If the user then moves the mouse, the viewport follows the pointer again.
- When the user holds `Tab` to rapidly cycle through many controls, a short debounce (target: ~100ms) prevents the viewport from frantically chasing every intermediate control. The viewport pans smoothly to wherever focus lands once the rapid cycling slows or stops.
- If the newly focused element is already visible within the current viewport, no panning occurs.
- Focus tracking relies on the Windows UI Automation (UIA) framework. Applications or custom controls that do not implement UIA focus events will not trigger viewport panning. This is a known and accepted limitation.

### 2.6 Text Cursor Following

When the user is typing or navigating in a text field, document, or code editor, the magnified viewport pans to keep the text cursor (caret) visible. This applies to caret movement caused by typing, arrow keys, `Home`, `End`, `Page Up/Down`, mouse click repositioning, and programmatic caret changes (e.g., Find & Replace jumping to a match).

**Key behavioral details:**

- During continuous typing, the viewport tracks the caret smoothly. A small lookahead margin keeps some whitespace visible ahead of the caret in the typing direction, preventing it from sitting at the very edge of the viewport.
- Caret following takes priority over pointer following when the user is actively typing. After a brief idle period with no keyboard input (target: ~500ms), pointer following resumes as the primary tracking source.
- Caret position is obtained via UI Automation `TextPattern` events where available, supplemented by polling `GetGUIThreadInfo` for the caret rectangle in applications where UIA text support is incomplete.

### 2.7 Temporary Zoom Toggle

The user holds a configurable key combination (default: `Ctrl+Alt`). If currently zoomed in, the screen smoothly animates to 1.0× for as long as the keys are held — allowing the user to briefly see the full desktop. Releasing the keys animates back to the previous zoom level. If not currently zoomed, holding the keys zooms to the user's last-used zoom level, and releasing returns to 1.0×.

**Key behavioral details:**

- The animation for the temporary toggle should be slightly longer than normal zoom transitions (target: 200–300ms) to give the user time to orient before the view stabilizes.
- The "previous zoom level" is stored in memory. If the user has never zoomed during the current session, the toggle zooms to the configured default zoom level (default: 2.0×).
- The toggle key combination must be configurable and must not conflict with the scroll-gesture modifier key.

### 2.8 Keyboard Shortcuts

In addition to scroll-gesture zoom, the application provides keyboard shortcuts as a secondary zoom control method.

**Provided shortcuts:**

| Action | Default Binding |
|--------|----------------|
| Zoom in one step | `Win + Plus` |
| Zoom out one step | `Win + Minus` |
| Toggle zoom off | `Win + Esc` |
| Open settings | `Win + Ctrl + M` |
| Temporary zoom toggle | `Ctrl + Alt` (hold) |

**Key behavioral details:**

- Keyboard-shortcut zoom in/out uses a configurable step size (default: 25%, configurable down to 5%).
- All keyboard-initiated zoom changes animate smoothly (see Section 2.2).
- Where SmoothZoom's default shortcuts overlap with native Windows Magnifier shortcuts, the application should document this and advise users to disable the native Magnifier if both are installed.

### 2.9 Settings and Configuration

The application provides a settings interface accessible via a system tray icon and via keyboard shortcut. All settings persist across sessions.

**Configurable settings:**

| Setting | Default | Range / Options |
|---------|---------|-----------------|
| Scroll-gesture modifier key | `Win` | `Win`, `Ctrl`, `Alt`, `Shift`, two-key combos |
| Temporary toggle key combo | `Ctrl+Alt` | Any two-key modifier combo |
| Minimum zoom level | 1.0× | 1.0× to 5.0× |
| Maximum zoom level | 10.0× | 2.0× to 10.0× |
| Keyboard shortcut zoom step | 25% | 5% to 100% |
| Zoom animation speed | Normal | Slow, Normal, Fast |
| Image smoothing | On | On / Off |
| Start with Windows | Off | On / Off |
| Start zoomed | Off | On / Off |
| Default zoom level (for toggle) | 2.0× | 1.5× to 10.0× |
| Follow keyboard focus | On | On / Off |
| Follow text cursor | On | On / Off |

**Key behavioral details:**

- Settings are persisted to a local configuration file (`%AppData%\SmoothZoom\config.json` or equivalent). No registry writes are required for basic operation.
- The system tray icon provides quick access to: open settings, toggle zoom on/off, and exit the application.
- Settings changes take effect immediately — no restart required.

### 2.10 Color Inversion

The user can toggle a color-inversion filter on the magnified view. When enabled, all colors in the magnified content are inverted (light becomes dark and vice versa). This is a common accessibility need for users who find light-on-dark easier to read.

**Key behavioral details:**

- Toggled via settings UI and via a keyboard shortcut (default: `Ctrl+Alt+I`, matching the native Magnifier convention).
- Implemented as a color transformation matrix applied to the magnified output — not a system-wide display setting.

---

## 3. Explicitly Out of Scope

The following features are **not** part of this project. Each is listed with a brief rationale to prevent scope creep and to provide context if the team later considers adding them.

### 3.1 Alternative Zoom View Modes

**Excluded: Lens mode, Docked mode, Picture-in-Picture, Split-screen.**

Rationale: These modes serve different use cases (occasional spot-checking, side-by-side reference) and each adds substantial UI surface area, configuration options, and rendering code paths. Full-screen zoom is the mode most commonly used as a daily driver by low-vision users, and is the mode where the macOS advantage is most pronounced. The other modes can be evaluated as a follow-up project if full-screen zoom is successful.

### 3.2 Alternative Viewport Tracking Modes

**Excluded: Edge-push mode, Pointer-centered mode.**

Rationale: Continuous tracking is the macOS default, the most intuitive for new users, and the mode that most clearly differentiates the experience from the native Windows Magnifier. Edge-push and pointer-centered modes are useful but are refinements, not essentials. They can be added in a future version without architectural changes.

### 3.3 Detach Zoom from Pointer

**Excluded: The ability to hold a key combination to temporarily freeze the viewport while moving the pointer freely.**

Rationale: This is a useful power-user feature on macOS, but it is a secondary interaction that depends on the user already being comfortable with the primary zoom experience. It can be added later as a settings option without rearchitecting the viewport tracking system.

### 3.4 Hover Text

**Excluded: A floating overlay that magnifies only the text under the pointer without zooming the full screen.**

Rationale: Hover Text is a separate accessibility feature on macOS that operates independently of Zoom. It is a distinct product with its own rendering pipeline and is not required to deliver the core full-screen zoom experience.

### 3.5 Text-to-Speech / Read Aloud

**Excluded: Built-in screen reading or text-to-speech capabilities.**

Rationale: The native Windows Magnifier includes a basic Read Aloud feature, and Windows also ships with Narrator. Duplicating speech synthesis is out of scope for a zoom tool. Users who need both magnification and speech should pair SmoothZoom with their preferred screen reader.

### 3.6 Advanced Multi-Monitor Features

**Excluded: Independent per-monitor zoom levels, using one monitor to display the magnified view of another, or zooming all monitors as a unified surface.**

Rationale: SmoothZoom will operate on the monitor where the pointer currently resides. When the pointer moves to a different monitor, zoom follows. Advanced multi-monitor scenarios (mirroring, independent zoom per display) add significant complexity around DPI handling, coordinate mapping, and monitor topology changes. These are post-v1 enhancements.

Basic multi-monitor awareness — correctly tracking the pointer as it crosses from one monitor to another and zooming the appropriate display — **is** in scope. Advanced multi-monitor orchestration is not.

### 3.7 Touch and Pen Input Zoom Gestures

**Excluded: Pinch-to-zoom on touchscreens, pen-based zoom activation.**

Rationale: Touch and pen input introduce separate input pipelines and gesture-recognition challenges. The core experience targets mouse and precision touchpad input. Touch/pen support can be layered in later.

### 3.8 Custom Color Filters Beyond Inversion

**Excluded: Grayscale, sepia, red/green filters, custom color transformation matrices.**

Rationale: Color inversion is included because it is the most commonly used color accessibility adjustment. Full color filter customization is a feature-rich domain (macOS offers it, Windows offers it system-wide in Display settings) that is not central to the zoom experience.

### 3.9 Notification Indicators While Zoomed

**Excluded: Flashing the screen edge or showing an indicator when a notification appears outside the current viewport.**

Rationale: A useful quality-of-life feature but not part of the core zoom-and-track experience. It can be added post-v1.

### 3.10 Replacing or Modifying the Native Windows Magnifier Binary

**Excluded: Patching, hooking into, or replacing `Magnify.exe` or any Windows system binaries.**

Rationale: SmoothZoom is a standalone application that uses public Windows APIs. It does not modify, replace, or tamper with any system files. Users who want SmoothZoom as their magnifier should disable the native Magnifier to avoid conflicts.

---

## 4. Assumptions

These are conditions the team is treating as true without further validation. If any assumption is invalidated, the scope or approach may need revision.

1. **The Windows Magnification API's full-screen mode will remain functional in current and near-future Windows releases.** Microsoft has marked the API documentation with a recommendation to use newer APIs, but the API itself has not been deprecated or removed. SmoothZoom will be architected to allow migration to the Desktop Duplication API or Windows.Graphics.Capture if the Magnification API is removed in a future Windows version.

2. **The Magnification API's `MagSetFullscreenTransform` accepts floating-point magnification factors with sufficient granularity for smooth animation.** Research confirms the factor is a `float`. The assumption is that calling this function at 60Hz+ with small incremental changes will produce visually smooth results without flicker, tearing, or excessive CPU/GPU load.

3. **A global low-level mouse hook (`WH_MOUSE_LL`) can reliably intercept scroll events with low enough latency for real-time zoom control.** Low-level hooks are a well-established Windows input interception mechanism, but they are subject to a system-enforced timeout. The assumption is that by keeping the hook callback minimal (post a message to a processing thread, return immediately) the hook will not be unregistered by the system.

4. **The `Win` key state can be reliably detected and the Start Menu activation can be reliably suppressed.** This technique is well-proven in shipping software (PowerToys, AutoHotkey, many games), but edge cases may exist with specific keyboard drivers or accessibility tools that remap the `Win` key. These should be identified during testing.

5. **Windows UI Automation provides sufficiently reliable focus-change and caret-position notifications for the applications where magnification is most commonly used** (web browsers, Office applications, text editors, file explorer, system dialogs). Coverage will not be 100% across all applications, and that is acceptable.

6. **The application will be code-signed for distribution.** The full-screen Magnification API requires `UIAccess="true"` in the application manifest, which in turn requires the binary to be digitally signed and installed in a secure location. The team has access to (or will obtain) a code-signing certificate.

7. **Target platform is Windows 10 version 1903 (May 2019 Update) and later, including Windows 11.** This covers the vast majority of the active Windows install base and ensures access to all required APIs.

---

## 5. Constraints

1. **Single full-screen magnifier limit.** Windows allows only one full-screen magnifier at a time. SmoothZoom cannot run simultaneously with the native Windows Magnifier in full-screen mode. The application must detect if the native Magnifier is active and advise the user accordingly.

2. **UIAccess and code-signing requirement.** The application must be signed and installed to a trusted location (e.g., `Program Files`). This limits "portable" or "run from Downloads folder" deployment scenarios for the full-screen magnifier functionality.

3. **64-bit only.** The Magnification API is not supported under WOW64 (32-bit process on 64-bit Windows). SmoothZoom must be built as a native 64-bit application.

4. **Secure desktop inaccessible.** The Magnification API does not function on the Windows secure desktop (Ctrl+Alt+Delete screen, UAC elevation prompts, lock screen). SmoothZoom's magnification will be unavailable on these surfaces. This matches the native Magnifier's behavior.

5. **DRM-protected content.** Some DRM-protected media content (e.g., Netflix in Edge, Blu-ray playback) may render as black in the magnified view due to hardware content protection. This is a platform-level restriction, not a defect in SmoothZoom.

6. **Win key interception scope.** When `Win` is used as the scroll-gesture modifier, SmoothZoom must intercept the `Win` key-up event to suppress the Start Menu. In the rare case that another application or utility also intercepts or remaps the `Win` key (e.g., certain gaming overlays, custom shell replacements), there may be conflicts. The user can switch to an alternative modifier key to resolve this.

---

## 6. Success Criteria

The project is considered successful when the following conditions are met:

### 6.1 Functional Criteria

- A user can hold `Win` (or their configured modifier) and scroll the mouse wheel to smoothly zoom the entire screen in and out, with the zoom centered on the pointer position.
- The Start Menu does not open when the user releases the `Win` key after performing a scroll-gesture zoom.
- Zoom transitions are visually smooth at 60fps with no perceptible stepping, jumping, or tearing under normal desktop workloads on hardware meeting minimum requirements.
- While zoomed, the viewport continuously follows the mouse pointer with no perceptible lag or jitter.
- While zoomed, pressing `Tab` or performing keyboard navigation causes the viewport to smoothly pan to the newly focused UI element.
- While zoomed and typing in a text field, the viewport keeps the text cursor visible.
- The temporary zoom toggle works: holding the configured keys smoothly zooms to/from 1.0×, releasing restores the previous state.
- All settings are configurable and persist across application restarts.

### 6.2 Performance Criteria

- Input-to-display latency for scroll-gesture zoom: ≤1 display frame (≤16ms at 60Hz).
- Steady-state CPU usage while zoomed and idle (pointer stationary): <2%.
- Steady-state CPU usage while zoomed and pointer is moving: <5%.
- GPU usage overhead: <10% of available GPU on integrated graphics (Intel UHD 620 class or equivalent).
- Memory footprint: <50MB resident.

### 6.3 Compatibility Criteria

- Functions correctly on Windows 10 1903+ and Windows 11.
- Functions correctly at all system DPI scaling levels (100%, 125%, 150%, 175%, 200%).
- Functions correctly on single-monitor setups and on multi-monitor setups where the pointer moves between monitors.
- Does not interfere with application behavior when zoom is at 1.0× (effectively inactive).
- Does not interfere with scroll behavior in any application when the `Win` key is not held.
- Pressing and releasing `Win` without scrolling still opens the Start Menu normally.

### 6.4 Minimum Hardware Requirements

- 64-bit Windows 10 version 1903 or later
- DirectX 11 compatible GPU with WDDM 2.0 driver
- 4 GB RAM
- Any x64 processor from the last 10 years

---

## 7. Terminology

| Term | Definition |
|------|-----------|
| **Zoom level** | The magnification factor. 1.0× = normal size. 2.0× = everything is twice as large. Maximum: 10.0×. |
| **Viewport** | The rectangular portion of the desktop that is currently visible on screen while zoomed in. At 2.0× zoom, the viewport covers one quarter of the total desktop area. |
| **Scroll-gesture zoom** | The interaction where the user holds the `Win` key (or configured modifier) and scrolls the mouse wheel or touchpad to change the zoom level. |
| **Continuous tracking** | The viewport tracking mode where the magnified view moves proportionally with the pointer at all times, not just when the pointer reaches an edge. |
| **Temporary toggle** | A hold-to-peek interaction: holding a key combination temporarily reverses the zoom state (zoomed ↔ unzoomed), and releasing restores it. |
| **Follow focus** | The behavior where the viewport pans to keep the currently focused UI element or text cursor visible. |
| **Modifier key** | A key (`Win`, `Ctrl`, `Alt`, or `Shift`) held in combination with another input (scroll wheel) to activate zoom. |
| **Start Menu suppression** | The technique of preventing the Start Menu from opening when the `Win` key is released after being used as a modifier for scroll-gesture zoom. |
| **Deadzone** | A small region around the pointer's position where tiny movements do not trigger viewport panning, preventing visual jitter when the pointer is nearly at rest. |
| **UIAccess** | A Windows application manifest flag that grants the application permission to interact with UI elements running at higher privilege levels, required for full-screen magnification. |
| **UIA** | Windows UI Automation — the framework used to receive notifications about focus changes and caret positions across applications. |

---

## 8. Document Roadmap

This is Document 1 of 5 in the Development Plan series:

| # | Document | Purpose |
|---|----------|---------|
| **1** | **Project Scope and Non-Scope** (this document) | Defines what is and is not being built, and the conditions for success. |
| 2 | Behavior Specification | Detailed acceptance criteria for each in-scope feature. Testable, unambiguous descriptions of exactly how each interaction should behave. |
| 3 | Technical Architecture | Component-level system design: input interception, animation engine, viewport tracker, Magnification API integration, settings persistence, and how they connect. |
| 4 | Phased Delivery Plan | Milestones that decompose the work into incremental, testable, shippable phases. Each phase produces a runnable build. |
| 5 | Technical Risks and Mitigations | Known risks, their likelihood, impact, and concrete mitigation strategies. |

# Research Report: Modifying Windows Magnifier to Behave Like macOS Screen Zoom

## 1. Executive Summary

This research document provides a detailed comparative analysis of the Windows Magnifier application and the macOS Screen Zoom accessibility feature, intended to serve as the foundation for a development plan to modify the Windows Magnifier to function and behave more like the macOS implementation. The macOS Zoom is widely praised for its smooth, intuitive scroll-gesture-based zoom activation, fine-grained control, and seamless integration with the desktop, while the Windows Magnifier is considered more utilitarian but less fluid in daily use. Bridging these UX gaps requires addressing input handling, zoom animation smoothness, pointer tracking behavior, zoom style versatility, and advanced customization options.

---

## 2. Current State: Windows Magnifier

### 2.1 Overview

Windows Magnifier (`Magnify.exe`) is a built-in accessibility utility that has been part of Windows since Windows 98. It was significantly improved in Windows 7 and again in Windows 10/11. It provides screen magnification from 100% to 1600% (16×) for users with visual impairments.

### 2.2 Activation and Controls

| Feature | Details |
|---------|---------|
| **Launch** | `Win + Plus (+)` to open; `Win + Esc` to close |
| **Zoom In/Out** | `Win + Plus` / `Win + Minus` |
| **Cycle Views** | `Ctrl + Alt + M` |
| **Settings** | `Win + Ctrl + M` |
| **Temp Full View** | `Ctrl + Alt + Spacebar` (shows entire screen briefly) |
| **Color Inversion** | `Ctrl + Alt + I` |

### 2.3 View Modes

Windows Magnifier offers three view modes:

1. **Full Screen Mode** — The entire screen is magnified. The viewport pans to follow the mouse, keyboard focus, or text cursor. This is the default and most commonly used mode.

2. **Lens Mode** — A movable rectangular lens follows the mouse pointer, magnifying only the area within the lens boundaries. The lens size is adjustable via `Ctrl + Alt + R` then dragging.

3. **Docked Mode** — A docked panel at the top of the screen shows a magnified view of the area near the mouse cursor. The rest of the desktop remains visible below.

### 2.4 Tracking Options

The magnified view can be configured to follow:
- Mouse pointer
- Keyboard focus
- Text cursor (caret)
- Narrator cursor

In Full Screen mode, there are two pointer-tracking behaviors:
- **Keep the mouse pointer in the center of the screen**
- **Keep the mouse pointer within the edges of the screen** (moves view only when the pointer reaches the viewport edge)

### 2.5 Configuration Options

- **Zoom level**: Adjustable from 100% to 1600%
- **Zoom increments**: 25%, 50%, 100%, 150%, 200%, 300%, 400% per step
- **Smooth edges**: Smooths magnified text/image edges (anti-aliasing)
- **Invert colors**: Inverts the color scheme of the magnified content
- **Start on sign-in**: Can be configured to auto-launch
- **Reading aloud**: Built-in text-to-speech for reading content under the cursor

### 2.6 Key Limitations

- **No scroll-gesture zoom**: Cannot hold a modifier key and scroll the mouse wheel to zoom — must use keyboard shortcuts or the Magnifier toolbar buttons.
- **Coarse zoom increments**: Minimum increment is 25%; no smooth, continuous zoom.
- **No animated zoom transitions**: Zoom changes are instantaneous/stepped, not smoothly animated.
- **Maximum 16× zoom**: Compared to macOS's 40× maximum.
- **No picture-in-picture floating window mode**: The Lens mode is somewhat similar but less flexible and polished.
- **No split-screen mode**: No way to show a zoomed region alongside the normal-size desktop in a split view.
- **No min/max zoom range customization**: Cannot set preferred minimum and maximum zoom bounds.
- **No temporary toggle**: Cannot temporarily engage zoom by holding a key combination and release to return to normal.
- **Limited pointer movement options**: Only two tracking modes in full screen, versus macOS's three (continuous, edge-triggered, center-locked).

### 2.7 Technical Implementation

- Windows Magnifier is backed by `Magnification.dll` which provides the **Magnification API**.
- The API supports two modes: **full-screen magnifier** (Windows 8+) and **magnifier control** (windowed, Windows Vista+).
- The magnification factor is a floating-point value (1.0 = no magnification).
- Color effects use a 5×5 color transformation matrix.
- The full-screen magnifier applies transformations to the entire screen via the DWM (Desktop Window Manager).
- Input transform mapping (`MagSetInputTransform`) maps magnified coordinates back to actual screen coordinates for pen/touch input.
- Applications using UIAccess="true" in their manifest and digitally signed can access the full-screen magnifier API.
- Microsoft now recommends the **Windows.Graphics.Capture** namespace or the **Desktop Duplication API** over the legacy Magnification API for new development.

---

## 3. Current State: macOS Screen Zoom

### 3.1 Overview

macOS Zoom is a built-in accessibility feature located under System Settings → Accessibility → Zoom. Unlike the Windows Magnifier, it is not a standalone application — it is deeply integrated into the operating system's compositing engine (Quartz Compositor / WindowServer). It supports magnification up to 40× and offers extremely smooth, GPU-accelerated zoom transitions.

### 3.2 Activation and Controls

| Feature | Details |
|---------|---------|
| **Toggle Zoom On/Off** | `Ctrl + Option + 8` |
| **Zoom In** | `Ctrl + Option + =` |
| **Zoom Out** | `Ctrl + Option + -` |
| **Scroll Gesture** | Hold modifier key (Ctrl, Option, or Command — configurable) + scroll wheel / trackpad scroll |
| **Temporary Toggle** | Hold `Ctrl + Option` to temporarily show zoomed/unzoomed state |
| **Detach from Pointer** | Hold `Ctrl + Command` to temporarily detach zoom view from pointer |
| **Toggle Full Screen / PiP** | `Option + Command + F` |
| **Adjust Zoom Window** | `Ctrl + Option + Command + Arrow Keys` (resize) / `Option + Command + Arrow Keys` (move) |

### 3.3 Zoom Styles

macOS offers three zoom styles:

1. **Full Screen Zoom** — Magnifies the entire display. The magnified viewport moves to follow the pointer. On multi-monitor setups, can zoom all displays as one, or assign a secondary display to show the magnified view.

2. **Split Screen Zoom** — Divides the display into two regions: one shows the magnified view, the other shows the normal desktop. The zoomed region and normal region can be resized.

3. **Picture-in-Picture (PiP) Zoom** — A resizable, movable floating rectangular window follows the cursor and shows a magnified view of the area beneath the pointer. The window can be made stationary or set to follow the cursor.

### 3.4 Scroll Gesture Zoom (Key Differentiator)

The single most distinctive feature of macOS Zoom is the **scroll gesture with modifier key**:

- The user enables "Use scroll gesture with modifier keys to zoom" in Accessibility settings.
- The user selects a modifier key: Control, Option, or Command (or a combination).
- To zoom: hold the modifier key and scroll up (zoom in) or scroll down (zoom out) using a mouse scroll wheel or trackpad two-finger scroll.
- The zoom is **smooth and continuous** — not stepped. The zoom level changes fluidly with the scroll velocity.
- This feels natural and intuitive, like a "digital camera zoom."
- Works with trackpads (two-finger scroll), Magic Mouse (single-finger scroll), and standard scroll wheels.

### 3.5 Advanced Zoomed Image Movement Options

macOS offers three distinct behaviors for how the magnified viewport moves relative to the pointer:

1. **Continuously with Pointer** — The zoomed viewport moves in real time as the pointer moves. The screen appears to "track" with the mouse at all times.

2. **When Pointer Reaches Edge** — The zoomed viewport stays stationary until the pointer touches the edge of the visible area, then it scrolls to follow. This reduces visual motion.

3. **To Keep Pointer Centred** — The zoomed viewport always moves to keep the pointer at the center of the screen. The pointer itself appears stationary while the desktop scrolls underneath it.

### 3.6 Additional Advanced Options

- **Restore zoom factor on startup**: Remembers the last zoom level.
- **Smooth images**: Applies anti-aliasing/image smoothing to magnified content.
- **Flash screen when notification appears**: Alerts the user to notifications that appear outside the zoomed viewport.
- **Minimum and Maximum zoom range**: Two separate sliders allow the user to define their preferred zoom range (e.g., minimum 1×, maximum 15×).
- **Follow keyboard focus**: The zoomed view follows keyboard focus changes.
- **Hover Text**: A separate feature that enlarges text under the pointer in a floating tooltip-like overlay (activated by holding Command).
- **Touch Bar Zoom**: For Macs with Touch Bar, enables a zoom mode that displays an enlarged Touch Bar on-screen.
- **Invert colors**: Can invert colors within the zoomed view.
- **Multi-monitor options**: Can zoom displays independently, use one display for the magnified view of another, or zoom all displays as one unified surface.
- **Speak items under pointer**: Text-to-speech for content under the cursor, with configurable delay.

### 3.7 Key Strengths Versus Windows

- **Smooth, continuous scroll-gesture zoom** — the single most cited advantage.
- **40× max zoom** versus Windows' 16×.
- **Three flexible pointer-tracking modes** with intuitive names and behaviors.
- **Temporary toggle** — hold keys to temporarily peek at the zoomed/unzoomed state.
- **Detach from pointer** — temporarily park the zoomed viewport while moving the mouse elsewhere.
- **PiP mode** — a truly floating, resizable, repositionable magnifier window.
- **Min/max zoom range sliders** — fine-grained control over zoom boundaries.
- **Deep OS integration** — GPU-accelerated, zero-latency compositing rather than bitmap capture and rescale.

---

## 4. Feature-by-Feature Gap Analysis

| Feature | Windows Magnifier | macOS Zoom | Gap Severity |
|---------|:--:|:--:|:--:|
| Full-screen zoom | ✅ | ✅ | None |
| Lens / PiP floating magnifier | ✅ (Lens) | ✅ (PiP, more flexible) | Medium |
| Docked / Split-screen view | ✅ (Docked, top only) | ✅ (Split, configurable) | Medium |
| Scroll-gesture zoom activation | ❌ | ✅ | **Critical** |
| Smooth, continuous zoom | ❌ (stepped) | ✅ | **Critical** |
| Animated zoom transitions | ❌ | ✅ | **High** |
| Temporary zoom toggle (hold-to-peek) | ❌ | ✅ | **High** |
| Detach zoom from pointer | ❌ | ✅ | Medium |
| Pointer movement: Continuous tracking | ✅ (center or edge) | ✅ | None |
| Pointer movement: Edge-triggered pan | ✅ | ✅ | None |
| Pointer movement: Keep pointer centered | ❌ (center mode exists but different) | ✅ | Low |
| Min/Max zoom range controls | ❌ | ✅ | Medium |
| Maximum zoom level | 16× | 40× | Medium |
| Zoom increment granularity | 25% minimum | Continuous (any value) | **High** |
| Color inversion | ✅ | ✅ | None |
| Image smoothing / anti-aliasing | ✅ | ✅ | None |
| Text-to-speech reading | ✅ | ✅ | None |
| Hover Text overlay | ❌ | ✅ | Low |
| Multi-monitor support | Basic | Advanced (independent, mirror, unified) | Medium |
| Follow keyboard focus | ✅ | ✅ | None |
| Follow text cursor | ✅ | ✅ | None |
| Auto-start at login | ✅ | ✅ | None |
| Remember zoom level on restart | ✅ | ✅ | None |
| Notification flash when zoomed | ❌ | ✅ | Low |

---

## 5. Technical Approaches for Implementation

### 5.1 Approach A: Enhance the Native Magnifier (AutoHotkey/Wrapper Approach)

This approach wraps or enhances the existing Windows Magnifier by intercepting input and programmatically controlling its behavior.

**Prior Art: AeroZoom** — An AutoHotkey-based tool that already adds macOS-like features to Windows Magnifier, including:
- Modifier key + scroll wheel zoom (simulates `Win + Plus/Minus` rapidly)
- Pinch-to-zoom support
- Adjustable zoom rate granularity (6 levels)
- Quick-access panel for Magnifier settings
- Elastic zoom (hold to zoom, release to unzoom)

**Pros:**
- No need to replace Magnifier — builds on what exists.
- Leverages the DWM-backed full-screen magnification.
- Lower development effort.
- AeroZoom provides a proven reference implementation.

**Cons:**
- Cannot achieve truly smooth/continuous zoom — must simulate it by rapidly incrementing the Magnification API factor.
- Input interception can conflict with other applications that use the same modifier key combinations.
- Limited by the Magnification API's capabilities and the existing Magnifier's behavior.
- Cannot add entirely new view modes (e.g., true split-screen).

### 5.2 Approach B: Custom Magnifier Using the Magnification API

Build a new application using the Windows Magnification API directly.

**Key API Functions:**
- `MagInitialize()` / `MagUninitialize()` — Initialize/cleanup the magnification runtime.
- `MagSetFullscreenTransform(magLevel, xOffset, yOffset)` — Set magnification factor and panning offset (Windows 8+).
- `MagGetFullscreenTransform()` — Query current magnification state.
- `MagSetFullscreenColorEffect(pEffect)` — Apply a 5×5 color transformation matrix.
- `MagSetInputTransform(fEnabled, pRectSource, pRectDest)` — Map magnified input coordinates back to screen coordinates.
- `MagSetWindowTransform(hwnd, pTransform)` — Set magnification on a windowed magnifier control.
- `MagSetWindowFilterList(hwnd, dwFilterMode, count, pHWND)` — Control which windows are magnified (windowed mode only).

**Implementation Details:**
- Full-screen magnifier requires `UIAccess="true"` in the application manifest and the binary must be digitally signed and placed in a secure location (e.g., `C:\Program Files\`).
- The magnification factor supports floating-point values, enabling smooth, sub-percentage zoom changes.
- Panning offset (xOffset, yOffset) can be set independently, enabling custom viewport tracking logic.
- A timer or animation loop can interpolate between zoom levels for smooth transitions.

**Pros:**
- Full control over zoom behavior, including smooth animation.
- Can implement all three macOS pointer-tracking modes.
- Can add scroll-gesture interception natively.
- Can implement temporary toggle and detach features.

**Cons:**
- Full-screen magnifier API only available on Windows 8+.
- Requires code signing and UIAccess elevation for full-screen mode.
- Not supported under WOW64 (must be 64-bit on 64-bit Windows).
- Microsoft has flagged the Magnification API as legacy, recommending newer APIs instead.

### 5.3 Approach C: Custom Magnifier Using Desktop Duplication API / Windows.Graphics.Capture

Build a fully custom magnifier using the recommended modern APIs.

**Desktop Duplication API (DXGI):**
- Available on Windows 8+.
- Provides direct access to the desktop frame buffer via DXGI Output Duplication.
- Allows capturing the desktop at GPU-native speed with minimal latency.
- The captured texture can be rendered into a Direct3D/Direct2D surface with arbitrary scaling and filtering.
- Supports smooth bilinear/trilinear filtering for high-quality magnification.

**Windows.Graphics.Capture:**
- Available on Windows 10 1903+.
- Higher-level WinRT API for screen capture.
- Supports capturing specific windows or the entire screen.
- Integrates with Direct3D 11 for GPU-accelerated rendering.
- Simpler API surface than raw DXGI duplication.

**Pros:**
- Future-proof — these are Microsoft's recommended APIs.
- GPU-accelerated capture and rendering for smooth performance.
- Complete creative freedom over zoom behavior, animation, and rendering quality.
- Can implement custom shader effects (e.g., smooth scaling, color filters).

**Cons:**
- Highest development effort.
- Must handle input transformation manually (click-through, cursor mapping).
- Must handle multi-monitor DPI scaling complexities.
- Requires careful management of cursor rendering in the magnified view.
- May face challenges with DRM-protected content, secure desktops, and UAC prompts.

### 5.4 Recommended Approach: Hybrid (B + elements of A)

The recommended approach is to build a custom application primarily using the **Magnification API for full-screen mode** (Approach B), enhanced with:
- A global low-level mouse hook to intercept scroll-gesture + modifier key for zoom control.
- An animation/interpolation layer that smoothly transitions the `MagSetFullscreenTransform` magnification factor and offset over time (using a high-frequency timer or `RequestAnimationFrame`-style loop).
- A windowed magnifier control mode for PiP functionality.
- Fallback to the wrapper approach (Approach A) for any scenario where the API is insufficient.

This balances development effort, integration quality, and future-proofing. The full-screen magnifier API handles the hard work of DWM integration, cursor rendering, and input mapping, while the custom layer adds the UX polish that macOS provides.

---

## 6. Implementation Priorities (Ranked by Impact)

### Priority 1: Scroll-Gesture Zoom Activation (Critical)

**Goal:** Hold a configurable modifier key (e.g., Ctrl) and scroll the mouse wheel to smoothly zoom in/out — exactly like macOS.

**Implementation:**
- Install a global low-level mouse hook (`SetWindowsHookEx` with `WH_MOUSE_LL`) to intercept `WM_MOUSEWHEEL` events.
- When the configured modifier key is held (check `GetAsyncKeyState`), consume the scroll event and translate scroll delta into a zoom factor change.
- Map scroll delta to a zoom-rate curve (e.g., logarithmic) for natural-feeling zoom progression.
- Apply the zoom change via `MagSetFullscreenTransform()`.
- Make the modifier key configurable (Ctrl, Alt, Shift, or combinations) in a settings UI.

### Priority 2: Smooth, Continuous Zoom Animation (Critical)

**Goal:** Zoom level changes should animate smoothly rather than snapping to discrete increments.

**Implementation:**
- Maintain a "target zoom level" and a "current zoom level."
- Use a high-frequency timer (e.g., 60Hz or matching the display refresh rate) to interpolate the current zoom toward the target using easing (e.g., exponential ease-out).
- Each frame, call `MagSetFullscreenTransform()` with the interpolated value.
- The Magnification API accepts floating-point zoom factors, so sub-percentage changes are supported.
- Ensure the viewport offset (pan) is also smoothly interpolated for fluid viewport movement.

### Priority 3: Temporary Zoom Toggle (High)

**Goal:** Hold a key combination to temporarily engage/disengage zoom, releasing the keys returns to the previous state.

**Implementation:**
- Register a global hotkey (e.g., `Ctrl + Alt`) that, when held, temporarily toggles the zoom state.
- On key-down: if zoomed, animate to 1× (or a preview rect); if not zoomed, animate to last zoom level.
- On key-up: revert to the previous state with a smooth animation.
- This is a state-machine approach: Normal → Peeking → Normal.

### Priority 4: Three-Mode Pointer Tracking (High)

**Goal:** Implement the three macOS pointer-tracking behaviors.

**Implementation:**
- **Continuous**: Calculate the viewport offset so the cursor is always near the center. Apply a subtle deadzone to prevent jitter.
- **Edge-triggered**: Only update the viewport offset when the cursor (in magnified coordinates) reaches the edge of the visible viewport. Use a scroll-like panning behavior.
- **Center-locked**: Always set the viewport offset to center the cursor position. The desktop scrolls, the pointer stays visually centered.
- Allow switching between these modes via settings.

### Priority 5: Enhanced Zoom Range and Granularity (Medium)

**Goal:** Support higher maximum zoom (up to 40×), configurable min/max bounds, and sub-percentage increments.

**Implementation:**
- The Magnification API supports floating-point factors; there is no inherent 16× hard limit in the API — the limit is in the Magnifier application itself.
- Allow users to set minimum zoom (e.g., 1.5×) and maximum zoom (e.g., 25×) via slider controls.
- Zoom increments for keyboard shortcuts should be configurable from 5% to 400%.

### Priority 6: Improved PiP / Floating Magnifier Window (Medium)

**Goal:** A floating, resizable, repositionable magnifier window similar to macOS PiP Zoom.

**Implementation:**
- Use the windowed Magnifier control API to create a magnifier window.
- Make the window resizable with drag handles.
- Allow the window to be positioned freely or set to follow the cursor.
- Add an option to make the window stationary (like macOS's "Keep picture-in-picture window stationary").
- Implement keyboard shortcuts to resize and reposition the window (mimicking macOS's `Ctrl+Option+Command+Arrow` shortcuts).

### Priority 7: Detach Zoom from Pointer (Medium)

**Goal:** Temporarily freeze the zoomed viewport while moving the mouse to interact with a different part of the screen.

**Implementation:**
- When the detach hotkey is held, stop updating the viewport offset while continuing to track the actual mouse position.
- When the key is released, smoothly animate the viewport to the cursor's current position.

### Priority 8: Notification Flash / Edge Indicator (Low)

**Goal:** Alert users to events (notifications, focus changes) occurring outside the zoomed viewport.

**Implementation:**
- Monitor notification APIs (`Shell_NotifyIcon`, WinRT toast notifications).
- When a notification appears outside the current viewport, briefly flash the screen edge in the direction of the notification.

### Priority 9: Settings UI and Persistence (Supporting)

**Goal:** A clean, accessible settings interface for all the above features.

**Implementation:**
- System tray icon with a settings panel.
- Mirror the macOS Accessibility → Zoom settings layout for familiarity.
- Persist all settings to the Windows Registry or a JSON configuration file.
- Support command-line arguments for enterprise deployment.

---

## 7. Existing Tools and Reference Implementations

| Tool | Type | Relevance |
|------|------|-----------|
| **AeroZoom** (github.com/wandersick/aerozoom) | AutoHotkey wrapper around Windows Magnifier | Direct prior art for macOS-like zoom on Windows. Open source. Implements scroll-wheel zoom, elastic zoom, adjustable zoom rate. |
| **Microsoft Magnification API Samples** (github.com/microsoft/Windows-classic-samples/tree/main/Samples/Magnification) | C++ reference code | Official samples for both full-screen and windowed magnifier using the API. Essential reference for implementation. |
| **Karna Magnification** (github.com/perevoznyk/karna-magnification) | .NET wrapper for Magnification API | C# P/Invoke wrappers for all Magnification API functions. Useful if implementing in .NET. |
| **ZoomText (Freedom Scientific)** | Commercial assistive technology | Industry-leading third-party magnifier. Provides a benchmark for professional-quality magnification features. |
| **ZoomGlass** (kinfolksoft.com) | Commercial screen magnifier | Hardware-accelerated magnifier with 60–120fps rendering, configurable quality, crosshair support. Reference for smooth rendering. |
| **Virtual Magnifying Glass** (magnifier.sourceforge.net) | Open-source cross-platform magnifier | Cross-platform magnifier for Windows/macOS/Linux. Useful reference for cross-platform magnification approaches. |
| **Sysinternals ZoomIt** | Presentation zoom tool | Lightweight zoom/annotation tool. AeroZoom integrates with it. Useful reference for zoom gesture handling. |

---

## 8. Technical Risks and Considerations

### 8.1 Code Signing and UIAccess

The full-screen Magnification API requires the application to be built with `UIAccess="true"` in the manifest, digitally signed, and installed in a secure location (e.g., `Program Files`). This adds deployment complexity. Self-signed certificates work for development but not for distribution.

### 8.2 Conflict with Native Magnifier

Only one full-screen magnifier can be active at a time. The custom application must detect and gracefully handle the case where the native Windows Magnifier is already running, and vice versa. Consider offering to disable the native Magnifier when the custom tool starts.

### 8.3 Global Hook Reliability

Global low-level mouse hooks (`WH_MOUSE_LL`) can be unreliable: Windows may unhook them if the hook procedure takes too long (the system enforces a timeout). The hook callback must be extremely fast, deferring actual zoom calculations to a separate thread.

### 8.4 Performance at High Zoom Levels

At very high zoom levels (>10×), the viewport covers a very small portion of the screen. Rapid mouse movements can cause the viewport to "jump" between distant screen regions. Smooth interpolation of the viewport position is critical to prevent disorientation.

### 8.5 Multi-Monitor and DPI Scaling

Windows environments often have multiple monitors with different DPI scales and resolutions. The magnifier must correctly handle DPI-aware coordinate transformations, per-monitor DPI changes, and scenarios where the cursor moves between monitors at different scales.

### 8.6 Secure Desktop and UAC

The Magnification API may not function on the secure desktop (Ctrl+Alt+Delete screen, UAC prompts). The application should gracefully degrade in these scenarios.

### 8.7 API Deprecation

Microsoft has placed deprecation notices on the Magnification API documentation, recommending `Windows.Graphics.Capture` and the Desktop Duplication API instead. While the Magnification API still functions in Windows 11, future Windows versions could remove or further restrict it. A migration path to the newer APIs should be planned.

---

## 9. Suggested Technology Stack

| Component | Recommended Technology |
|-----------|----------------------|
| **Language** | C++ (for direct Magnification API access) or C# with P/Invoke |
| **Full-screen magnification** | Magnification API (`Magnification.dll`) |
| **Windowed / PiP magnification** | Magnification API windowed control, or Desktop Duplication API with Direct2D rendering |
| **Input interception** | Global low-level hooks (`SetWindowsHookEx` with `WH_MOUSE_LL` and `WH_KEYBOARD_LL`) |
| **Animation loop** | High-resolution multimedia timer (`timeSetEvent`) or `SetTimer` at ≥60Hz |
| **Settings persistence** | Windows Registry or `AppData\Local` JSON file |
| **Settings UI** | WinUI 3 (modern look), WPF, or Win32 dialog |
| **Installer** | MSIX or Inno Setup (handles code signing, secure folder placement) |

---

## 10. Summary of Key Development Goals

1. **Scroll-gesture zoom**: Hold modifier key + scroll wheel = smooth zoom in/out (the #1 priority and most impactful change).
2. **Smooth zoom animation**: All zoom level changes should animate fluidly, not step.
3. **Three pointer-tracking modes**: Continuous, edge-triggered, center-locked — matching macOS exactly.
4. **Temporary zoom toggle**: Hold keys to temporarily peek at the zoomed/unzoomed state.
5. **Detach zoom from pointer**: Temporarily freeze the zoomed viewport for mouse interaction elsewhere.
6. **Flexible PiP mode**: A floating, resizable, repositionable magnifier window.
7. **Extended zoom range**: Up to 40× with configurable min/max bounds.
8. **Configurable settings UI**: Accessible, persistent, with sensible defaults modeled after macOS Zoom settings layout.
9. **Robust multi-monitor support**: Handle mixed DPI, resolution, and multi-display configurations.
10. **Clean, modern codebase**: Built on supported APIs with a migration path away from deprecated components.

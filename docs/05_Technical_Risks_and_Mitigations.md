# Technical Risks and Mitigations

## macOS-Style Full-Screen Zoom for Windows

### Document 5 of 5 — Development Plan Series

**Version:** 1.0
**Status:** Draft
**Last Updated:** February 2026
**Prerequisites:** Documents 1–4 of this series

---

## 1. How to Read This Document

Each risk is assigned a unique identifier (R-01 through R-22) and rated on two dimensions:

**Likelihood** — how probable is it that this risk materializes?
- **Low:** Unlikely given current evidence, but not impossible.
- **Medium:** A realistic possibility that should be planned for.
- **High:** Expected to occur based on known platform behavior or prior experience.

**Impact** — if the risk materializes, how severe is the consequence?
- **Low:** A minor inconvenience. A workaround exists and no user-visible degradation occurs.
- **Medium:** A noticeable degradation in one feature area. The core experience still functions.
- **High:** A major feature is broken or seriously degraded. The product is shippable but diminished.
- **Critical:** The technical approach is invalidated. A fundamental redesign is required before the project can proceed.

**First Exposed** indicates the delivery phase (from Document 4) where the risk is first testable. Risks exposed in Phase 0 are the most important to resolve early — that's why Phase 0 exists.

The risks are organized into seven categories and ordered within each category by the product of likelihood and impact (highest concern first).

---

## 2. Category A — Magnification API Risks

These risks concern the Windows Magnification API (`Magnification.dll`), which is the sole rendering pathway for SmoothZoom's magnified output.

---

### R-01: Magnification API Deprecation or Removal

| Attribute | Value |
|-----------|-------|
| Likelihood | **Medium** |
| Impact | **Critical** |
| First Exposed | Phase 0 |

**Description:** Microsoft's documentation for the Magnification API includes a recommendation to use the Desktop Duplication API or `Windows.Graphics.Capture` for new development. While the API has not been formally deprecated (no removal timeline has been announced), a future Windows update could restrict, break, or remove it. If the API stops functioning, SmoothZoom has no rendering output.

**Current Evidence:** The Magnification API remains fully functional in Windows 11 23H2 and 24H2. The native Windows Magnifier (`Magnify.exe`) still ships in Windows 11 and still uses this API. Microsoft is unlikely to remove an API that their own shipping accessibility tool depends on without providing an equivalent replacement. However, the deprecation notice signals directional intent.

**Mitigation:**
1. The architecture isolates all Magnification API calls behind a single component (MagBridge). No other component has a direct dependency on the API. This bounds the migration effort to one component.
2. During Phase 0, verify that the API functions correctly on the latest Windows 11 Insider Preview builds. These builds provide ~6 months of advance warning before changes reach stable releases.
3. Monitor the Windows Developer Blog, Windows SDK release notes, and the Magnification API documentation page for any changes to the deprecation notice.

**Contingency:** If the API is removed or broken in a future Windows release, replace MagBridge's internals with a Desktop Duplication API or `Windows.Graphics.Capture` implementation. This replacement would capture the desktop framebuffer each frame, apply scaling and color transforms via Direct3D/Direct2D shaders, and render the result to a full-screen topmost overlay window. The migration is architecturally bounded but represents a significant development effort (estimated: comparable to Phase 0 + Phase 1 combined) because it requires manual handling of input coordinate remapping, cursor rendering, and DWM compositing that the Magnification API currently provides for free.

---

### R-02: Float-Precision Zoom Quantization

| Attribute | Value |
|-----------|-------|
| Likelihood | **Low** |
| Impact | **Critical** |
| First Exposed | Phase 0 |

**Description:** The Magnification API's `MagSetFullscreenTransform` accepts a `float` magnification level. The assumption is that this float is honored at full precision — a value of 1.073 produces a different visual result than 1.074. If the API internally rounds to coarser steps (e.g., nearest 0.25 or nearest 0.1), smooth scroll-gesture zoom would appear stepped, defeating the core value proposition.

**Current Evidence:** The API documentation specifies a `float` parameter with no mention of quantization. The AeroZoom project (which rapidly calls `MagSetFullscreenTransform` to simulate smooth zoom) reports visually smooth results, suggesting that float precision is honored. However, the AeroZoom approach calls the function at ~20Hz, not 60Hz+ with sub-0.01 increments, so its evidence is not conclusive at SmoothZoom's target granularity.

**Mitigation:**
1. Phase 0's exit criterion E0.2 directly tests this: increment the zoom level by 0.01 per frame in a tight loop and visually verify smooth results.
2. Test on at least three GPU configurations (Intel integrated, NVIDIA discrete, AMD discrete) since the DWM rendering path may differ.

**Contingency:** If the API quantizes to visually noticeable steps (e.g., 0.05 or larger): investigate whether the quantization is GPU-driver-dependent (solvable by driver updates or GPU-specific code paths) or inherent to the API (requires migration to Desktop Duplication / Graphics.Capture rendering, same as R-01 contingency).

---

### R-03: MagSetFullscreenTransform Latency Exceeding One Frame

| Attribute | Value |
|-----------|-------|
| Likelihood | **Low** |
| Impact | **High** |
| First Exposed | Phase 0 |

**Description:** The Magnification API may internally buffer transform changes, causing the visual update to appear one or more frames after the API call. If the latency exceeds one frame period (16.67ms at 60Hz), scroll-gesture zoom will feel laggy and disconnected from the user's input.

**Current Evidence:** The API operates through the Desktop Window Manager (DWM) compositing pipeline. DWM composes the next frame using the most recently submitted transforms, which suggests the update should appear on the next VSync. However, there is no Microsoft documentation guaranteeing a specific latency bound.

**Mitigation:**
1. Phase 0's exit criterion E0.3 tests this by visual inspection. For objective measurement, log the timestamp of each `MagSetFullscreenTransform` call alongside a DWM present timestamp (obtainable via `DwmGetCompositionTimingInfo`).
2. Call `MagSetFullscreenTransform` as late as possible in the frame (just before `DwmFlush`) to minimize the window between the API call and the VSync.

**Contingency:** If latency is consistently one extra frame (total: 2 frames / ~33ms at 60Hz): this is tolerable for keyboard-initiated animations but may feel slightly sluggish for scroll-gesture zoom. Explore whether calling the API on a higher-priority thread or using `DwmSetPresentParameters` can reduce the delay. If latency is variable or exceeds two frames: this becomes a blocking issue requiring the Desktop Duplication migration (R-01 contingency).

---

### R-04: MagSetInputTransform Desynchronization

| Attribute | Value |
|-----------|-------|
| Likelihood | **Medium** |
| Impact | **High** |
| First Exposed | Phase 1 |

**Description:** `MagSetInputTransform` maps physical screen coordinates to magnified desktop coordinates so that mouse clicks, hovers, and drags hit the correct elements. If the input transform falls even slightly out of sync with the visual transform (e.g., the input transform lags by one frame, or uses stale offset values), clicks will miss their targets. At 5× zoom, a one-pixel coordinate error in the input transform maps to a five-pixel miss in the magnified view. This is a subtle, user-hostile bug that may not be obvious during casual testing but becomes infuriating during sustained use.

**Mitigation:**
1. Always call `MagSetInputTransform` in the same code block — same frame tick, same thread, same set of values — as `MagSetFullscreenTransform`. Never defer one call to a later frame.
2. Compute the input transform source/destination rectangles from the same zoom level and offset values used for the visual transform. Do not read these values separately; pass them as a single parameter set.
3. Build a focused test case: at 5× zoom, position a 10×10 pixel button at various screen locations and verify that clicking the visible button always activates it. Test at screen edges, corners, and center.

**Contingency:** If desynchronization occurs despite same-frame updates: investigate whether the DWM applies the visual transform and the input transform at different points in the compositing pipeline. If so, experiment with applying the input transform one frame ahead of the visual transform (predictive correction).

---

## 3. Category B — Input Interception Risks

These risks concern the global low-level hooks that intercept mouse and keyboard events.

---

### R-05: Hook Deregistration Under System Load

| Attribute | Value |
|-----------|-------|
| Likelihood | **High** |
| Impact | **High** |
| First Exposed | Phase 1 |

**Description:** Windows enforces a timeout on low-level hook callbacks. If the hook procedure does not return within approximately 300ms (configurable via the `LowLevelHooksTimeout` registry value, default varies by Windows version), Windows silently removes the hook. This is a well-documented behavior that has affected many shipping applications. Once the hook is removed, SmoothZoom stops intercepting scroll events — the modifier+scroll gesture silently stops working. The user has no feedback that anything has gone wrong.

**Contributing factors that increase likelihood:**
- The main thread's message pump stalls briefly due to a COM call, a dialog box, a Windows Update notification, or system-level message processing.
- The user's antivirus software delays message dispatch to the hook thread.
- A system-wide broadcast message (`SendMessage(HWND_BROADCAST, ...)`) from another application blocks the message pump.

**Mitigation:**
1. The hook callback does the absolute minimum work: read the event struct, perform one or two atomic writes, return. All computation is deferred to the Render Thread. This keeps the callback well under 1ms per invocation.
2. A watchdog timer on the main thread checks the hook handle validity every 5 seconds. If the hook has been deregistered, it re-installs it immediately and logs a warning.
3. On re-registration failure (unlikely but possible if the system is in a degraded state), display a tray notification informing the user that scroll-gesture zoom is temporarily unavailable.
4. Avoid any blocking operation on the main thread's message pump: no synchronous COM calls, no modal dialogs from hook callbacks, no file I/O.

**Contingency:** If hook deregistration becomes frequent in testing (multiple times per hour): investigate alternative input interception mechanisms. Raw Input (`RegisterRawInputDevices` with `RIDEV_INPUTSINK`) can receive mouse scroll events without a hook, though it cannot consume them (prevent forwarding). A hybrid approach — Raw Input for low-latency reading, with the hook only used for consumption of modifier+scroll events — may be more robust.

---

### R-06: Start Menu Suppression Failure

| Attribute | Value |
|-----------|-------|
| Likelihood | **Medium** |
| Impact | **Medium** |
| First Exposed | Phase 1 |

**Description:** When the user releases the Win key after using it for scroll-gesture zoom, the WinKeyManager injects a dummy `Ctrl` keystroke to prevent the Start Menu from opening. This suppression technique is well-established (PowerToys, AutoHotkey, and many games use it), but it can fail in specific circumstances:

- A third-party shell replacement (e.g., StartAllBack, Open-Shell) hooks the Win key at a lower level and processes the Start Menu activation before SmoothZoom's injected keystroke arrives.
- A gaming overlay or keyboard remapping tool (e.g., SharpKeys, AutoHotkey script) modifies the Win key event chain.
- Windows itself changes the Start Menu activation logic in a future update (this has happened before: Windows 10 and Windows 11 have slightly different Win key handling).

**Consequence:** The Start Menu opens every time the user releases the Win key after zooming. This is annoying but not functionally blocking — the user can close the Start Menu and continue using zoom.

**Mitigation:**
1. Test the suppression technique on Windows 10 (22H2), Windows 11 (23H2, 24H2), and the latest Windows 11 Insider Preview.
2. Test with popular third-party Start Menu replacements (StartAllBack, Open-Shell) installed.
3. If the `Ctrl` injection technique fails on a specific configuration, try alternative suppression methods: inject `VK_NONAME`, inject `Shift` instead of `Ctrl`, or use `keybd_event` instead of `SendInput` (some applications respond differently to the two injection APIs).
4. Document the known conflict with specific third-party tools and advise users to switch the modifier key to `Ctrl` or `Alt` if they experience the issue.

**Contingency:** If suppression proves unreliable across mainstream configurations: change the default modifier key from Win to another key (e.g., `Ctrl+Alt` as a two-key modifier), and offer Win as a "supported but may conflict" option. This is a significant UX trade-off since Win+Scroll is the cleanest gesture, but reliability is more important.

---

### R-07: Modifier Key Conflicts with Other Applications

| Attribute | Value |
|-----------|-------|
| Likelihood | **Medium** |
| Impact | **Medium** |
| First Exposed | Phase 1 (Win), Phase 5 (configurable modifiers) |

**Description:** When SmoothZoom consumes scroll events while the modifier key is held, those events never reach the target application. This is by design, but it creates conflicts:
- **Ctrl+Scroll:** Consumed by browsers (zoom), VS Code (font size), Office apps (zoom), and many other applications. If the user configures `Ctrl` as the modifier, all Ctrl+Scroll functionality in all applications is lost.
- **Alt+Scroll:** Used by some applications for horizontal scrolling or tab cycling.
- **Win+Scroll:** No known conflicts with mainstream applications. This is why it is the default.
- **Shift+Scroll:** Used by some applications for horizontal scrolling.

**Mitigation:**
1. Win is the default modifier precisely because it has no known scroll-combination conflicts.
2. The settings UI for alternative modifiers includes a clear warning about the trade-off (AC-2.1.21).
3. The application never consumes scroll events when the modifier key is NOT held — this is a hard requirement (AC-2.1.02).

**Contingency:** If users discover a Win+Scroll conflict with a specific application: document it and offer the alternative modifier keys. If a Win+Scroll conflict is found with a widely-used application, evaluate whether a two-key modifier (e.g., `Ctrl+Win+Scroll`) is practical as the new default.

---

### R-08: Touchpad Scroll Event Format Variations

| Attribute | Value |
|-----------|-------|
| Likelihood | **Medium** |
| Impact | **Low** |
| First Exposed | Phase 1 |

**Description:** Windows Precision Touchpads send `WM_MOUSEWHEEL` messages with fine-grained delta values (sub-120 increments, representing continuous finger movement). Non-Precision Touchpads vary: some send standard 120-increment deltas, some send rapid sequences of smaller deltas, and some use vendor-specific touchpad drivers that may format events differently. SmoothZoom's scroll interception assumes `WM_MOUSEWHEEL` with delta values in `WHEEL_DELTA` units (120 per notch). If a touchpad sends events in an unexpected format, zoom behavior may be too fast, too slow, or jittery.

**Mitigation:**
1. Normalize all scroll delta values by `WHEEL_DELTA` (120) before applying the logarithmic zoom model. This handles both standard notch-based mice and fine-grained touchpads correctly.
2. Test on at least three touchpad configurations: a Precision Touchpad (e.g., Microsoft Surface, recent ThinkPad), a non-Precision Synaptics touchpad, and an external mouse with a free-spinning scroll wheel (e.g., Logitech MX Master with ratchet mode disabled).

**Contingency:** If a specific touchpad configuration produces unusable zoom behavior: add a sensitivity multiplier to the settings that the user can adjust. This is a last-resort affordance — the goal is for the default to work well on all input devices.

---

## 4. Category C — UI Automation Risks

These risks concern the Windows UI Automation (UIA) framework used for focus following and caret tracking.

---

### R-09: Inconsistent UIA Focus Event Support Across Applications

| Attribute | Value |
|-----------|-------|
| Likelihood | **High** |
| Impact | **Medium** |
| First Exposed | Phase 3 |

**Description:** UIA focus events are only as reliable as the accessibility implementation of each application. Some applications fire focus events correctly, some fire them with incorrect bounding rectangles, some fire them late, and some don't fire them at all. The Behavior Specification (AC-2.5.13, AC-2.5.14) establishes a "must work" list and a graceful-degradation contract, but the reality is that focus following will be imperfect in some applications.

**Known problem areas:**
- **Electron-based applications** (VS Code, Slack, Discord): Chromium's UIA support is generally good but has known issues with certain UI patterns (tree views, virtual scrolling containers).
- **Java applications** (JetBrains IDEs, some enterprise tools): The Java Access Bridge provides UIA support, but it can be slow and occasionally reports incorrect element coordinates.
- **Custom-drawn applications** (games, some media players, proprietary LOB software): No UIA support at all. Focus following silently does nothing.
- **Win32 legacy applications** without explicit accessibility markup: May report focus changes but with incorrect or missing bounding rectangles.

**Mitigation:**
1. Test the application compatibility matrix defined in Document 4, §6.3.5. Allocate significant manual testing effort for Phase 3.
2. Design the FocusMonitor to validate every bounding rectangle it receives: reject rectangles with zero area, negative dimensions, or coordinates obviously off-screen. This prevents the viewport from jumping to nonsensical positions when an application reports garbage data.
3. Implement a per-application fallback heuristic: if focus events are received but with consistently bad rectangles, fall back to pointer tracking for that application and log the issue.
4. The `followKeyboardFocus` setting (AC-2.9.08) allows the user to disable focus following entirely if it misbehaves in their workflow.

**Contingency:** If focus following is unreliable in one or more "must work" applications (Edge, Chrome, Firefox, Word, Excel): investigate application-specific workarounds. For example, Chrome and Edge expose focus information through the `IAccessible2` interface in addition to UIA; an alternative query path may yield better results.

---

### R-10: Caret Position Detection Failures

| Attribute | Value |
|-----------|-------|
| Likelihood | **High** |
| Impact | **Medium** |
| First Exposed | Phase 3 |

**Description:** Caret (text cursor) tracking is more fragile than focus tracking because it requires either UIA TextPattern support (rare outside of rich text controls) or the `GetGUIThreadInfo` fallback, which has its own limitations:

- `GetGUIThreadInfo` only reports the caret position for the foreground thread's window. If focus is in a child process (e.g., a browser tab running in a separate process), it may not see the caret.
- `GetGUIThreadInfo` reports the caret in client coordinates of the caret-owning window. If the window's DPI virtualization is active (non-DPI-aware application on a high-DPI display), the coordinates may be incorrect without additional correction.
- Some applications draw their own caret without using the standard Windows caret API (`CreateCaret` / `SetCaretPos`). In these applications, `GetGUIThreadInfo` reports no caret at all. Notable examples: Visual Studio Code (draws its own cursor), Windows Terminal (uses a custom rendering pipeline), and most web-based text editors rendered inside a browser.
- UIA TextPattern is implemented by relatively few applications. Notepad (Windows 11 version), Word, and some WPF applications support it. Chrome and Firefox have partial TextPattern support that may not report caret positions correctly for all web page text inputs.

**Mitigation:**
1. Use UIA TextPattern as the preferred path when available; fall back to `GetGUIThreadInfo` polling otherwise. This two-technique approach maximizes coverage.
2. For `GetGUIThreadInfo`, apply DPI correction: query the DPI awareness context of the caret-owning window and adjust coordinates if necessary using `LogicalToPhysicalPointForPerMonitorDPI`.
3. When no caret is detected by either technique, silently fall back to pointer tracking. The user should never see an error or experience degraded behavior in other features (AC-2.6.11).
4. Test caret following in every application on the compatibility matrix. Accept and document the applications where caret following does not work.

**Contingency:** If caret detection is unreliable in a critical application (e.g., Chrome web page text inputs): investigate a third technique — monitoring the `EVENT_OBJECT_LOCATIONCHANGE` WinEvent for the caret object. Some applications that don't support UIA TextPattern or the standard caret API do fire `OBJID_CARET` location change events. This is a lower-level, more intrusive approach but may catch cases the other two techniques miss.

---

### R-11: UIA Callback Latency Blocking Other Functionality

| Attribute | Value |
|-----------|-------|
| Likelihood | **Medium** |
| Impact | **Medium** |
| First Exposed | Phase 3 |

**Description:** UIA event callbacks execute on the UIA Thread. If a callback blocks (because the target application's accessibility provider is slow to respond to queries like `CurrentBoundingRectangle`), the UIA Thread stalls. This doesn't directly affect the Render Thread or the Main Thread (by design — the threading model isolates them), but it means focus and caret events are delayed or dropped until the UIA Thread recovers.

**Known slow providers:** Java applications via the Java Access Bridge are notorious for slow UIA responses. Some Electron applications also have measurable callback latency (50–200ms).

**Mitigation:**
1. The UIA Thread is already isolated from the Render Thread and Main Thread. This is the primary mitigation: a slow UIA callback can never drop a frame or delay a hook response.
2. Add a timeout to bounding rectangle queries: if `CurrentBoundingRectangle` doesn't return within 100ms, abandon the query and skip the focus/caret update for that event. The viewport simply doesn't pan for that focus change, which is better than stalling all accessibility tracking.
3. Log slow callbacks (>50ms) to help identify problematic applications during testing.

**Contingency:** If a critical application consistently produces UIA callbacks exceeding the timeout: investigate caching the last-known bounding rectangle for that application's elements and using it as an estimate until a fresh query completes.

---

## 5. Category D — Deployment and Signing Risks

---

### R-12: UIAccess Code Signing Complexity

| Attribute | Value |
|-----------|-------|
| Likelihood | **High** |
| Impact | **Medium** |
| First Exposed | Phase 0 |

**Description:** `MagSetFullscreenTransform` silently fails (returns `FALSE`, no error dialog, no exception) if the calling process does not have `uiAccess="true"` in its manifest, is not digitally signed, or is not running from a secure location (`Program Files`, `Windows\System32`, etc.). This creates three classes of deployment problems:

1. **Development friction:** Every debug build must be signed (self-signed is sufficient) and run from a secure folder, or the core API simply doesn't work. This slows the edit-compile-test cycle.
2. **Distribution cost:** A code-signing certificate from a trusted CA costs $200–$500/year. An Extended Validation (EV) certificate (required for immediate SmartScreen reputation) costs more.
3. **User installation restrictions:** Users cannot "just download and run" the application from their Downloads folder. It must be installed to `Program Files`, which may require administrator elevation.

**Mitigation:**
1. **Development:** Create a one-time development signing setup script that generates a self-signed certificate, installs it to the machine's Trusted Root CA store, and signs the build output. Integrate this into the build system so signing is automatic for debug builds.
2. **Distribution:** Budget for a standard code-signing certificate. Consider open-source signing services (e.g., SignPath) or organizational certificates if available.
3. **Installation:** Use an installer (Inno Setup, WiX, or MSIX) that handles elevation, secure folder placement, and optional Start Menu shortcut creation. The installer runs elevated once; the application itself runs at standard user privilege with UIAccess.

**Contingency:** If the code-signing cost is prohibitive: release the application as open-source with build instructions, allowing users to self-sign. This limits the audience to technical users but removes the financial barrier. A portable (unsigned) mode that provides all features except full-screen magnification is not feasible — the entire application depends on the Magnification API.

---

### R-13: Windows SmartScreen Blocking Initial Distribution

| Attribute | Value |
|-----------|-------|
| Likelihood | **High** |
| Impact | **Low** |
| First Exposed | Phase 5 (first user-facing distribution) |

**Description:** Windows SmartScreen flags executables from unknown publishers with a warning dialog ("Windows protected your PC"). This dialog appears before the application even launches, and many users will not click "Run anyway." Building SmartScreen reputation requires either an EV certificate (provides immediate reputation) or accumulated positive telemetry over time (standard certificate, takes days to weeks of user installations before the warning disappears).

**Mitigation:**
1. If budget permits, use an EV code-signing certificate for distribution builds.
2. If using a standard certificate: distribute the application through a website with clear installation instructions that explain the SmartScreen dialog and assure users it is expected for new software.
3. Submit the signed installer to Microsoft for manual SmartScreen review (available through the Microsoft Partner Center).

**Contingency:** This is a distribution friction issue, not a functional issue. The application works correctly regardless of SmartScreen warnings. Time and user adoption will naturally resolve the reputation problem.

---

## 6. Category E — Runtime Failure and Recovery Risks

---

### R-14: Crash Leaving Screen Magnified

| Attribute | Value |
|-----------|-------|
| Likelihood | **Medium** |
| Impact | **High** |
| First Exposed | Phase 1 |

**Description:** If SmoothZoom crashes (unhandled exception, stack overflow, external termination via Task Manager) while the screen is zoomed, the Magnification API's transform remains applied. The user sees a frozen magnified view with no way to revert it except launching Magnifier and resetting zoom, or rebooting. This is a hostile failure mode — the user's desktop becomes partially unusable.

**Mitigation:**
1. Install a `SetUnhandledExceptionFilter` handler that attempts `MagSetFullscreenTransform(1.0, 0, 0)` before the process terminates. This handles most crash scenarios (null pointer dereference, division by zero, etc.).
2. Install a `SetConsoleCtrlHandler` to catch `CTRL_CLOSE_EVENT`, `CTRL_LOGOFF_EVENT`, and `CTRL_SHUTDOWN_EVENT`.
3. Handle `WM_ENDSESSION` to clean up on system shutdown and user logoff.
4. Write a "dirty shutdown" sentinel file when the application starts. Remove it on clean shutdown. On next launch, if the sentinel file exists, force a `MagSetFullscreenTransform(1.0, 0, 0)` call before doing anything else, in case the previous instance left the screen magnified.

**Residual risk:** If the process is killed by `TerminateProcess` (e.g., Task Manager's "End Process" on some Windows versions), no handler runs. The sentinel-file approach handles this on the next launch, but the user's screen remains magnified until then.

**Contingency:** As a last-resort user recovery mechanism: document that running `Magnify.exe` and then closing it resets the full-screen transform. Alternatively, provide a tiny standalone "SmoothZoom Reset" utility (unsigned, no UIAccess needed — it just calls `MagSetFullscreenTransform(1.0, 0, 0)` from a signed helper) that the user can run from any location.

---

### R-15: Secure Desktop and UAC Prompt Disruption

| Attribute | Value |
|-----------|-------|
| Likelihood | **High** |
| Impact | **Low** |

**Description:** The Magnification API does not operate on the Windows secure desktop (Ctrl+Alt+Delete screen, UAC elevation prompts, lock screen). When the system switches to the secure desktop, the magnified view disappears and the user sees the unmagnified secure desktop. When the system returns to the normal desktop, the magnification must resume at its previous state. This behavior matches the native Windows Magnifier, so it is expected — but the transition can be jarring if not handled gracefully.

**Mitigation:**
1. Detect secure desktop transitions by monitoring for session change events (`WTSRegisterSessionNotification` with `NOTIFY_FOR_THIS_SESSION`) and `WM_WTSSESSION_CHANGE` messages.
2. When a session lock or secure desktop event is detected: note the current zoom state but take no action (the Magnification API will simply stop having effect).
3. When the session unlocks / returns to the normal desktop: reapply the current zoom transform and input transform. This ensures the user returns to their zoomed view seamlessly.
4. Do not crash or display errors during the transition (AC-ERR.04).

**Contingency:** None needed. This is a platform limitation that all magnifiers share. The key is handling it gracefully rather than crashing.

---

### R-16: Display Configuration Change While Zoomed

| Attribute | Value |
|-----------|-------|
| Likelihood | **Medium** |
| Impact | **Medium** |
| First Exposed | Phase 6 |

**Description:** If the user connects or disconnects a monitor, changes display resolution, or changes DPI scaling while SmoothZoom is actively zoomed, the viewport offset and proportional mapping calculations may reference stale screen geometry. Consequences range from the viewport being slightly mispositioned (minor) to the application crashing due to an out-of-bounds offset (severe).

**Mitigation:**
1. Register for `WM_DISPLAYCHANGE` messages, which Windows sends when the display resolution or configuration changes.
2. Register for `WM_DPICHANGED` messages on any owned windows (the settings window, the tray icon's hidden message window).
3. On either event: re-query all monitor geometry via `EnumDisplayMonitors` and `GetMonitorInfo`. Update ViewportTracker's screen dimensions. Re-clamp the current viewport offset to the new valid range. If the active monitor has been disconnected, transition zoom to the monitor where the pointer now resides.
4. Test: change resolution while zoomed, connect/disconnect a monitor while zoomed, change DPI scaling while zoomed.

**Contingency:** If edge cases prove difficult to handle (e.g., the pointer is on a monitor that is being removed mid-frame): implement a conservative fallback that resets zoom to 1.0× on any display configuration change, then allows the user to re-zoom. This is inelegant but safe. The fallback can be replaced with more sophisticated handling over time.

---

### R-17: Floating-Point Accumulation Error Over Long Sessions

| Attribute | Value |
|-----------|-------|
| Likelihood | **Medium** |
| Impact | **Low** |
| First Exposed | Phase 1 |

**Description:** The scroll-gesture zoom model applies a multiplicative factor to the current zoom level on every scroll event (`newZoom = currentZoom * factor`). Over a long session with thousands of zoom-in and zoom-out operations, floating-point rounding errors could accumulate, causing the zoom level to drift slightly from the expected value. For example, zooming in from 1.0× to 2.0× and then zooming back out by the same number of scroll notches might land at 0.9997× or 1.0003× instead of exactly 1.0×.

**Mitigation:**
1. When the computed zoom level is within a small epsilon (0.005) of 1.0×, snap it to exactly 1.0×. This ensures the user can always return to the unmagnified state.
2. Similarly, snap to the configured maximum when within epsilon of it.
3. Use `double` precision for internal zoom calculations and convert to `float` only at the API call boundary. This provides approximately 15 significant digits of precision, making accumulation error negligible over any realistic session duration.

**Contingency:** If drift is noticeable despite double precision (extremely unlikely): implement periodic zoom-level normalization, where the zoom level is periodically rounded to the nearest 0.001.

---

## 7. Category F — Performance Risks

---

### R-18: High CPU Usage from Render Loop at High Refresh Rates

| Attribute | Value |
|-----------|-------|
| Likelihood | **Medium** |
| Impact | **Medium** |
| First Exposed | Phase 1 |

**Description:** The Render Thread calls `DwmFlush()` once per VSync to pace its frame ticks. On a 60Hz display, this is 60 iterations per second — negligible CPU. On a 144Hz or 240Hz display, the render loop runs 144–240 times per second. If the per-frame work (reading shared state, computing offsets, calling the Magnification API) is not extremely cheap, CPU usage on high-refresh-rate displays may exceed the 5% target.

**Mitigation:**
1. The per-frame work is designed to be minimal: a few atomic reads, one or two multiplications, one or two API calls. No memory allocation, no file I/O, no mutex acquisition. Target: <0.1ms per frame tick.
2. When the zoom level is 1.0× and no animation is active, the render loop should skip the API calls entirely (AC-2.3.13). The `DwmFlush()` call still blocks, but the wake-up immediately goes back to sleep without doing any work.
3. When the zoom level is stable (not changing) and the pointer is stationary, the render loop detects that neither zoom nor offset has changed since the last frame and skips the API calls. Only `DwmFlush()` is called.

**Contingency:** If CPU usage remains too high on 240Hz displays even with the optimizations above: implement an adaptive frame rate where the render loop runs at a lower rate (e.g., 60Hz) when no input activity is occurring, and ramps up to full VSync rate only during active zooming or pointer movement.

---

### R-19: Viewport Instability at Very High Zoom Levels

| Attribute | Value |
|-----------|-------|
| Likelihood | **Medium** |
| Impact | **Medium** |
| First Exposed | Phase 1 |

**Description:** At 10× zoom, the viewport covers only 10% of the desktop in each dimension. A small mouse movement (e.g., 50 pixels) maps to a large viewport shift (50 pixels × the proportional mapping factor). If the user moves the mouse quickly at high zoom, the viewport can appear to "teleport" between distant desktop regions, which is disorienting. The proportional mapping formula itself is correct, but the resulting sensitivity may feel uncontrollable.

**Mitigation:**
1. The deadzone (AC-2.4.09) helps with micro-jitter but does not address rapid large movements.
2. Test the proportional mapping feel at zoom levels 5×, 8×, and 10× with typical mouse sensitivities (800 DPI, 1600 DPI, 3200 DPI). If the viewport movement feels too sensitive, consider applying a non-linear damping curve to the pointer-to-offset mapping at high zoom levels — small movements map linearly, but very fast movements are slightly attenuated.
3. Any damping must not introduce perceptible lag during slow, deliberate movements (AC-2.4.07, AC-2.4.10).

**Contingency:** If high-zoom usability is poor despite damping: add a user-configurable "tracking sensitivity" slider (not currently in scope but architecturally trivial to add to ViewportTracker). Alternatively, consider this a motivation for implementing edge-push tracking mode in a future version — which is better suited to high-zoom precision work.

---

## 8. Category G — Environmental and Compatibility Risks

---

### R-20: Anti-Cheat and Security Software Conflicts

| Attribute | Value |
|-----------|-------|
| Likelihood | **Medium** |
| Impact | **Medium** |
| First Exposed | Phase 1 |

**Description:** Some anti-cheat engines (EAC, BattlEye, Vanguard) and enterprise endpoint security products (CrowdStrike, Carbon Black) monitor or restrict processes that install global hooks, use UIAccess, or inject keystrokes via `SendInput`. SmoothZoom does all three. Potential consequences:
- The anti-cheat blocks hook installation, preventing SmoothZoom from functioning while a protected game is running.
- The security product flags SmoothZoom as suspicious behavior and quarantines it.
- SmoothZoom's `SendInput` calls (for Start Menu suppression) are blocked or logged as potential keystroke injection.

**Mitigation:**
1. SmoothZoom should detect when it cannot install hooks (the `SetWindowsHookEx` call returns `NULL`) and display a clear message suggesting the cause may be security software.
2. The application should be signed with a reputable certificate, which reduces the likelihood of security software flagging it.
3. SmoothZoom does not need to function while a full-screen game is running (games typically have their own zoom/magnification features). If a game's anti-cheat blocks hooks, this is acceptable — SmoothZoom simply becomes dormant until the game exits.
4. Document known interactions with popular anti-cheat and security products.

**Contingency:** If a specific enterprise security product consistently blocks SmoothZoom: work with the security vendor's allowlisting process to get the signed binary approved. This is a per-deployment issue, not a product-level fix.

---

### R-21: Sleep, Hibernate, and Fast User Switching

| Attribute | Value |
|-----------|-------|
| Likelihood | **High** |
| Impact | **Low** |
| First Exposed | Phase 1 |

**Description:** When the system enters sleep or hibernate, the display is powered off and the DWM compositing pipeline is suspended. When the system resumes, the display may have changed configuration (e.g., a laptop was docked during sleep and undocked on wake, or a monitor was disconnected). Additionally, fast user switching (switching to a different user account without logging out) may affect the Magnification API's state.

**Mitigation:**
1. Handle `WM_POWERBROADCAST` messages with `PBT_APMRESUMEAUTOMATIC` and `PBT_APMSUSPEND` to detect sleep/resume cycles.
2. On resume: re-validate monitor geometry (same as R-16 handling), re-verify hook installation (hooks may need re-registration after resume), and reapply the current magnification transform.
3. Handle `WM_WTSSESSION_CHANGE` with `WTS_SESSION_LOCK` and `WTS_SESSION_UNLOCK` to detect user switching and screen lock.
4. On session unlock: same re-validation as resume.

**Contingency:** If the Magnification API enters an inconsistent state after resume (e.g., `MagSetFullscreenTransform` silently fails): call `MagUninitialize()` and `MagInitialize()` to reset the API, then reapply the transform. This is a heavier recovery but should handle any stale API state.

---

### R-22: Keyboard Layout and Virtual Key Code Variations

| Attribute | Value |
|-----------|-------|
| Likelihood | **Medium** |
| Impact | **Low** |
| First Exposed | Phase 2 |

**Description:** The keyboard shortcuts `Win+Plus` and `Win+Minus` use virtual key codes `VK_OEM_PLUS` and `VK_OEM_MINUS`. On US keyboard layouts, these correspond to the `=`/`+` and `-`/`_` keys as expected. On some international layouts, these physical keys produce different characters and may map to different virtual key codes. For example:
- On German (QWERTZ) layouts, `+` is on a different physical key than on US layouts.
- On French (AZERTY) layouts, `=` requires `Shift`.
- Some keyboard drivers report non-standard virtual key codes for these keys.

The native Windows Magnifier has this same issue — `Win+Plus`/`Win+Minus` may not work on all international layouts.

**Mitigation:**
1. Accept multiple virtual key codes for zoom-in: `VK_OEM_PLUS`, `VK_ADD` (numpad plus), and the scan code for the physical key in the standard position.
2. Accept multiple virtual key codes for zoom-out: `VK_OEM_MINUS`, `VK_SUBTRACT` (numpad minus).
3. In Phase 5, keyboard shortcut bindings are user-configurable. Users on non-US layouts can remap to keys that work for them.

**Contingency:** If specific international layouts prove problematic: add layout detection logic that maps the zoom-in and zoom-out actions to the physically correct keys for the detected layout. This is a polish item, not a blocking issue — scroll-gesture zoom (the primary interaction) is unaffected.

---

## 9. Risk Summary Matrix

| ID | Risk | Likelihood | Impact | First Phase | Category |
|----|------|-----------|--------|-------------|----------|
| R-01 | Magnification API deprecation or removal | Medium | Critical | 0 | API |
| R-02 | Float-precision zoom quantization | Low | Critical | 0 | API |
| R-03 | MagSetFullscreenTransform latency | Low | High | 0 | API |
| R-04 | Input transform desynchronization | Medium | High | 1 | API |
| R-05 | Hook deregistration under load | High | High | 1 | Input |
| R-06 | Start Menu suppression failure | Medium | Medium | 1 | Input |
| R-07 | Modifier key conflicts with apps | Medium | Medium | 1 | Input |
| R-08 | Touchpad scroll event variations | Medium | Low | 1 | Input |
| R-09 | Inconsistent UIA focus events | High | Medium | 3 | UIA |
| R-10 | Caret position detection failures | High | Medium | 3 | UIA |
| R-11 | UIA callback latency | Medium | Medium | 3 | UIA |
| R-12 | UIAccess code signing complexity | High | Medium | 0 | Deployment |
| R-13 | SmartScreen blocking distribution | High | Low | 5 | Deployment |
| R-14 | Crash leaving screen magnified | Medium | High | 1 | Recovery |
| R-15 | Secure desktop disruption | High | Low | 1 | Recovery |
| R-16 | Display configuration change while zoomed | Medium | Medium | 6 | Recovery |
| R-17 | Floating-point accumulation error | Medium | Low | 1 | Recovery |
| R-18 | High CPU at high refresh rates | Medium | Medium | 1 | Performance |
| R-19 | Viewport instability at high zoom | Medium | Medium | 1 | Performance |
| R-20 | Anti-cheat and security software conflicts | Medium | Medium | 1 | Environment |
| R-21 | Sleep, hibernate, and user switching | High | Low | 1 | Environment |
| R-22 | Keyboard layout / virtual key variations | Medium | Low | 2 | Environment |

---

## 10. Top Five Risks by Priority

The following five risks warrant the most attention because they combine high likelihood, high impact, or both:

**1. R-01 (API Deprecation) — Medium likelihood, Critical impact.** The entire rendering pipeline depends on a single API that Microsoft has signaled it wants to retire. Mitigation is strong (MagBridge isolation, monitoring), but the contingency (Desktop Duplication migration) is expensive. This is the single largest long-term risk to the project.

**2. R-05 (Hook Deregistration) — High likelihood, High impact.** Hooks being silently removed is a known, documented Windows behavior. It will happen. The watchdog-and-reinstall mitigation reduces impact to a brief interruption, but it introduces a window where scroll events are not intercepted. The hook callback must be kept absolutely minimal.

**3. R-14 (Crash Leaving Screen Magnified) — Medium likelihood, High impact.** An application crash that leaves the user's desktop zoomed in with no recovery path is one of the worst failure modes possible. The multi-layered mitigation (exception handler, sentinel file, helper utility) addresses this, but the residual risk from `TerminateProcess` means it cannot be eliminated entirely.

**4. R-04 (Input Transform Desync) — Medium likelihood, High impact.** Clicks hitting the wrong elements is a subtle, infuriating bug that erodes trust in the tool. Same-frame updates are the primary mitigation, but the DWM compositing pipeline is opaque, and there may be inherent latency between the visual and input transforms. Extensive click-accuracy testing at multiple zoom levels is essential.

**5. R-09 (Inconsistent UIA Focus Events) — High likelihood, Medium impact.** UIA is unreliable across the application landscape. Focus following will not work perfectly everywhere. The scope document and behavior spec already account for this (graceful degradation, per-application "must work" vs. "best effort" tiers), but user expectations may still exceed what is achievable. Clear documentation and the ability to disable focus following are the most important mitigations.

# Technical Architecture

## macOS-Style Full-Screen Zoom for Windows

### Document 3 of 5 — Development Plan Series

**Version:** 1.0
**Status:** Draft
**Last Updated:** February 2026
**Prerequisites:** Document 1 — Project Scope (v1.1), Document 2 — Behavior Specification (v1.0)

---

## 1. Architecture Overview

SmoothZoom is structured as a single-process Win32 desktop application consisting of ten components organized into four layers. Events flow downward from the Input Layer through processing in the Logic Layer, with the Output Layer making the single API call that changes what the user sees. The Support Layer provides settings and UI services to all other layers.

```
┌─────────────────── Input Layer ────────────────────┐
│                                                     │
│   InputInterceptor          FocusMonitor            │
│   (hooks: mouse, keyboard)  (UIA focus events)      │
│                                                     │
│   WinKeyManager             CaretMonitor            │
│   (Win key state,           (UIA text + GTTI poll)  │
│    Start Menu suppression)                          │
│                                                     │
├─────────────────── Logic Layer ────────────────────-┤
│                                                     │
│   ZoomController            ViewportTracker         │
│   (zoom level state,        (offset computation,    │
│    scroll accumulation,      proportional mapping,  │
│    animation targets)        priority arbitration)  │
│                                                     │
│               RenderLoop                            │
│               (frame tick, interpolation,           │
│                calls MagBridge each frame)           │
│                                                     │
├─────────────────── Output Layer ───────────────────-┤
│                                                     │
│               MagBridge                             │
│               (Magnification API wrapper)           │
│                                                     │
├─────────────────── Support Layer ──────────────────-┤
│                                                     │
│   SettingsManager           TrayUI                  │
│   (config.json, observers)  (tray icon, settings    │
│                              window, context menu)  │
│                                                     │
└─────────────────────────────────────────────────────┘
```

---

## 2. Threading Model

SmoothZoom uses three threads. The threading model is deliberately simple — more threads would add synchronization complexity without meaningful performance benefit, because the bottleneck is the single call to `MagSetFullscreenTransform` per frame, which is fast.

### 2.1 Main Thread

**Responsibilities:**
- Win32 message pump (`GetMessage` / `DispatchMessage` loop).
- Global low-level hook installation and callbacks (`WH_MOUSE_LL`, `WH_KEYBOARD_LL`). These hooks require a message pump on the installing thread.
- Hook callbacks execute here but do minimal work: they read the event, update shared state or post a message, and return immediately. The system-enforced hook timeout (typically ~300ms, configurable via `LowLevelHooksTimeout` registry key) is never approached.
- TrayUI message handling (system tray, context menu, settings window).
- `MagInitialize()` is called on this thread during startup.
- Application lifecycle management (startup, shutdown, graceful exit animation).

**Why hooks live here:** Low-level hooks must be on a thread with a message pump. Since the main thread already has one, colocating the hooks avoids creating an additional thread solely for that purpose. The critical requirement is that the hook callbacks are fast — they should not perform any computation, I/O, or blocking operations.

### 2.2 Render Thread

**Responsibilities:**
- Runs a tight loop timed to the display's vertical sync (VSync) interval. On a 60Hz display this is approximately every 16.67ms. On 120Hz, every 8.33ms.
- Each iteration ("frame tick") performs three steps:
  1. Read shared state: current zoom target, viewport target, animation parameters.
  2. Compute interpolated values if an animation is in progress.
  3. Call `MagSetFullscreenTransform(zoomLevel, xOffset, yOffset)` and optionally `MagSetFullscreenColorEffect(matrix)`.
- Also calls `MagSetInputTransform` when the zoom level or offset changes, to keep input coordinate mapping in sync with the display.

**Timing mechanism:** The render thread uses `DwmFlush()` as its synchronization primitive. `DwmFlush` blocks until the next VSync, providing natural frame pacing tied to the display's actual refresh rate. This avoids both under-shooting (wasted CPU on polling) and over-shooting (rendering frames that will never be displayed). Fallback: if `DwmFlush` is unavailable or unreliable, a multimedia timer (`timeSetEvent` at 1ms resolution) with sleep-to-target-time logic provides an alternative.

**Why a separate thread:** The main thread's message pump cannot guarantee sub-frame dispatch latency. Windows messages can be delayed by other message processing, COM marshaling, or UI operations. A dedicated render thread ensures that frame ticks are never blocked by message pump activity, meeting the ≤16ms latency requirement (AC-2.2.01).

### 2.3 UIA Thread

**Responsibilities:**
- Hosts the UI Automation event subscriptions for the FocusMonitor and CaretMonitor components.
- UIA event handlers fire on this thread. They extract the relevant data (bounding rectangle, caret position) and post it to the shared state for the ViewportTracker to consume.
- Runs its own COM-initialized message pump (`CoInitializeEx` with `COINIT_MULTITHREADED` or `COINIT_APARTMENTTHREADED` as UIA requires).

**Why separate:** UIA event callbacks can be slow or block briefly depending on the accessibility provider implementation of the target application. Isolating them from the main thread prevents a slow UIA callback from delaying hook processing or UI responsiveness. Isolating them from the render thread prevents a slow callback from dropping a frame.

### 2.4 Thread Communication

Threads communicate through shared state structures protected by lightweight synchronization:

| Shared State | Written By | Read By | Protection |
|-------------|-----------|---------|------------|
| Modifier key state (bool) | Main (hook callback) | Main (hook callback) | Atomic |
| Pointer position (x, y) | Main (hook callback) | Render | Atomic pair or SeqLock |
| Scroll delta accumulator | Main (hook callback) | Render | Atomic |
| Keyboard shortcut commands | Main (hook callback) | Render | Lock-free queue |
| Toggle state | Main (hook callback) | Render | Atomic |
| Focus target rectangle | UIA | Render | SeqLock |
| Caret target rectangle | UIA | Render | SeqLock |
| Last keyboard input timestamp | Main (hook callback) | Render | Atomic |
| Last focus change timestamp | UIA | Render | Atomic |
| Settings snapshot | Main (SettingsManager) | All | Copy-on-write (atomic pointer swap) |

**SeqLock** is used for small structs (rectangle = 4 integers) where the writer is infrequent and the reader is frequent. The reader retries if it detects a concurrent write. This avoids mutex contention on the render thread's hot path.

**No heap allocation on the hot path.** The render thread's per-frame tick must not allocate memory, acquire a mutex, or perform any operation that could block or trigger a page fault. All per-frame data is read from pre-allocated shared state.

---

## 3. Component Specifications

### 3.1 InputInterceptor

**Purpose:** Installs global low-level hooks, receives raw input events, classifies them, and routes them to the appropriate consumer.

**Hooks installed:**

| Hook | Events Captured | Purpose |
|------|----------------|---------|
| `WH_MOUSE_LL` | `WM_MOUSEWHEEL`, `WM_MOUSEHWHEEL`, `WM_MOUSEMOVE` | Scroll events for zoom, pointer position for tracking |
| `WH_KEYBOARD_LL` | `WM_KEYDOWN`, `WM_KEYUP`, `WM_SYSKEYDOWN`, `WM_SYSKEYUP` | Modifier key state, keyboard shortcuts, toggle keys |

**Hook callback behavior (critical for reliability):**

The mouse hook callback:
1. Reads `MSLLHOOKSTRUCT` from `lParam`.
2. If the event is `WM_MOUSEWHEEL` and the modifier key is currently held:
   a. Extracts scroll delta from `HIWORD(mouseData)`.
   b. Atomically adds delta to the scroll accumulator.
   c. Sets `winKeyUsedForZoom = true` (if the modifier is Win).
   d. Returns `1` (consume the event — do not pass to next hook).
3. If the event is `WM_MOUSEMOVE`:
   a. Writes pointer position (x, y) to shared state.
   b. Returns `CallNextHookEx(...)` (pass through).
4. All other events: `CallNextHookEx(...)`.

The keyboard hook callback:
1. Reads `KBDLLHOOKSTRUCT` from `lParam`.
2. Checks for modifier key down/up, shortcut key combinations, and toggle key combinations.
3. Delegates Win key state management to WinKeyManager.
4. For recognized shortcuts (zoom in/out, toggle, settings, color inversion): posts a command to the keyboard command queue.
5. For all keyboard events: records timestamp to `lastKeyboardInputTime`.
6. Returns `CallNextHookEx(...)` for all keyboard events (never consumes keyboard events, only observes them — except see WinKeyManager below for the suppression case).

**Hook health monitoring:** A watchdog timer on the main thread periodically verifies that the hooks are still installed by checking the hook handles. If a handle becomes invalid (the system unregistered the hook), it re-installs the hook and logs a warning (AC-ERR.03).

### 3.2 WinKeyManager

**Purpose:** Tracks the state of the `Win` key and handles Start Menu suppression. This is factored out of InputInterceptor because the logic is stateful and has subtle edge cases that warrant isolation and focused testing.

**State machine:**

```
         Win key down
IDLE ─────────────────► WIN_HELD
  ▲                        │
  │                        ├── Scroll event consumed
  │                        │   → set usedForZoom = true
  │                        │
  │                        ├── Other key pressed (e.g., E for Win+E)
  │                        │   → set usedWithOtherKey = true
  │                        │
  │   Win key up           │
  │◄───────────────────────┘
  │
  │   On Win key up:
  │   if usedForZoom:
  │       inject suppression keystroke, DO NOT pass Win key-up through
  │   else:
  │       pass Win key-up through normally (Start Menu opens)
```

**Suppression technique:** When the Win key is released after being used for zoom, the callback injects a dummy `Ctrl` key press/release (via `SendInput` with `KEYEVENTF_KEYUP` | `KEYEVENTF_KEYDOWN` for `VK_CONTROL`) before passing the Win key-up to the next hook. This causes the shell to interpret the sequence as `Win+Ctrl` (a combination that has no Start Menu effect) rather than a bare `Win` release.

**Important:** When the modifier key is not `Win` (user changed it to `Ctrl`, `Alt`, or `Shift`), WinKeyManager is disabled entirely. It does nothing.

### 3.3 ZoomController

**Purpose:** Owns the authoritative zoom level state. All zoom changes flow through this component. It enforces bounds, applies the scroll-gesture logarithmic model, manages keyboard-shortcut animation targets, and handles the temporary toggle save/restore.

**State:**

```
float currentZoom        = 1.0    // The actual zoom level right now
float targetZoom         = 1.0    // The target for animated transitions
float savedZoomForToggle = 0.0    // Saved level for temporary toggle (0 = none)
bool  toggleActive       = false  // Whether the temporary toggle is held
enum  mode               = IDLE   // IDLE, SCROLL_DIRECT, ANIMATING, TOGGLING
```

**Zoom modes:**

| Mode | Entry Condition | Zoom Behavior | Exit Condition |
|------|----------------|---------------|----------------|
| `IDLE` | No active zoom input | `currentZoom` = `targetZoom`, no changes | Any zoom input |
| `SCROLL_DIRECT` | Scroll delta received while modifier held | `currentZoom` updated directly from accumulated scroll delta. No interpolation — changes apply on the next render frame. | Modifier key released (transition to IDLE or ANIMATING depending on whether a keyboard animation was pending) |
| `ANIMATING` | Keyboard shortcut pressed | `targetZoom` set to stepped value. RenderLoop interpolates `currentZoom` toward `targetZoom` each frame. | `currentZoom` reaches `targetZoom` (within epsilon), or interrupted by scroll/toggle |
| `TOGGLING` | Toggle keys pressed | `savedZoomForToggle` = `currentZoom`. `targetZoom` set to 1.0× (or to saved/default if at 1.0×). Animates. | Toggle keys released → `targetZoom` = `savedZoomForToggle`, animate back. On completion → IDLE |

**Scroll-gesture zoom calculation (logarithmic model):**

```
// Called each time scroll delta is consumed from the accumulator
function applyScrollDelta(rawDelta, pointerPos):
    // rawDelta is in WHEEL_DELTA units (typically 120 per notch)
    normalizedDelta = rawDelta / 120.0

    // Logarithmic scaling: multiply current zoom by a factor per notch
    // scrollZoomFactor is tunable; 1.1 means ~10% per notch
    factor = scrollZoomFactor ^ normalizedDelta    // e.g., 1.1^1 = 1.1 for one notch up

    newZoom = currentZoom * factor
    newZoom = clamp(newZoom, settings.minZoom, settings.maxZoom)

    currentZoom = newZoom
    targetZoom  = newZoom
```

This produces the perceptually-even zoom behavior specified in AC-2.1.06: the same scroll effort doubles the zoom from 1× to 2× as from 5× to 10×.

**Keyboard-shortcut step calculation (additive model):**

```
function applyKeyboardStep(direction):  // direction: +1 or -1
    step = settings.keyboardZoomStep    // e.g., 0.25 for 25%
    newTarget = targetZoom + (direction * step)
    newTarget = clamp(newTarget, settings.minZoom, settings.maxZoom)
    targetZoom = newTarget
    mode = ANIMATING
```

**Bounds clamping with soft approach (AC-2.1.15):**

```
// Soft clamping: as zoom approaches a bound, the effective scroll
// delta is attenuated so the zoom decelerates rather than hitting a wall.
function softClamp(zoom, min, max):
    margin = 0.05 * (max - min)  // 5% of range as soft zone [tunable]
    if zoom > max - margin:
        t = (zoom - (max - margin)) / margin   // 0..1 within soft zone
        attenuation = 1.0 - t                  // fades to 0 at the bound
        // Apply attenuation to the scroll delta before computing newZoom
    if zoom < min + margin:
        // Mirror logic for lower bound
    return clamp(zoom, min, max)
```

### 3.4 ViewportTracker

**Purpose:** Computes the target viewport offset (x, y) each frame based on the current tracking source (pointer, focus, or caret). Manages priority arbitration between sources and smooth transitions when switching sources.

**Core algorithm — proportional pointer mapping:**

```
function computePointerOffset(pointerX, pointerY, zoom, screenW, screenH):
    // This formula has the elegant property that the desktop point
    // under the pointer is always (pointerX, pointerY), regardless
    // of zoom level. This means zoom-center stability comes for free:
    // zooming in/out while the pointer is stationary keeps the same
    // desktop content under the pointer.

    xOffset = pointerX * (1.0 - 1.0 / zoom)
    yOffset = pointerY * (1.0 - 1.0 / zoom)

    // Clamp to valid offset range (viewport cannot pan past desktop edges)
    maxOffsetX = screenW * (1.0 - 1.0 / zoom)
    maxOffsetY = screenH * (1.0 - 1.0 / zoom)
    xOffset = clamp(xOffset, 0, maxOffsetX)
    yOffset = clamp(yOffset, 0, maxOffsetY)

    return (xOffset, yOffset)
```

**Key mathematical property:** At any zoom level Z, the desktop coordinate under screen position (px, py) is:

```
desktopX = xOffset + px / Z = px(1 - 1/Z) + px/Z = px
desktopY = yOffset + py / Z = py(1 - 1/Z) + py/Z = py
```

The desktop point under the pointer equals the pointer's screen position, regardless of zoom. This means that when the user scroll-zooms while the pointer is stationary, the content under the pointer remains fixed — the zoom-center behavior specified in AC-2.1.09 through AC-2.1.12 emerges naturally without separate zoom-center tracking code.

**Focus/caret offset computation:**

When the tracking source is focus or caret, the target offset positions the viewport to center the target rectangle on screen:

```
function computeElementOffset(elementRect, zoom, screenW, screenH):
    // elementRect is in desktop (unmagnified) coordinates
    // Center the element in the viewport
    viewportW = screenW / zoom
    viewportH = screenH / zoom

    centerX = elementRect.centerX
    centerY = elementRect.centerY

    xOffset = centerX - viewportW / 2.0
    yOffset = centerY - viewportH / 2.0

    // Clamp to desktop bounds
    maxOffsetX = screenW - viewportW
    maxOffsetY = screenH - viewportH
    xOffset = clamp(xOffset, 0, maxOffsetX)
    yOffset = clamp(yOffset, 0, maxOffsetY)

    return (xOffset, yOffset)
```

**Caret lookahead:** When tracking a caret, the offset computation shifts the target slightly to provide a lookahead margin ahead of the caret in the typing direction:

```
function computeCaretOffset(caretRect, zoom, screenW, screenH):
    // Shift the target to keep margin ahead of the caret
    lookahead = (screenW / zoom) * 0.15  // 15% of viewport width [tunable]
    shiftedRect = caretRect
    shiftedRect.centerX += lookahead     // Assumes LTR text; negate for RTL

    return computeElementOffset(shiftedRect, zoom, screenW, screenH)
```

**Priority arbitration:**

The ViewportTracker maintains an `activeSource` enum and timestamps:

```
enum TrackingSource { POINTER, FOCUS, CARET }

function determineActiveSource():
    now = currentTimeMs()

    // Caret takes priority while user is actively typing
    if (now - lastKeyboardInputTime) < caretIdleTimeout:  // 500ms [tunable]
        if caretRectangle is valid:
            return CARET

    // Focus takes priority if a focus change occurred recently
    // and the user hasn't moved the mouse since
    if (lastFocusChangeTime > lastPointerMoveTime):
        if focusRectangle is valid:
            return FOCUS

    // Default: pointer
    return POINTER
```

**Source transitions:** When `activeSource` changes, the ViewportTracker does NOT snap the offset to the new source's target. Instead, it sets a transition flag and the RenderLoop smoothly interpolates from the current offset to the new target over ~200ms (ease-out). This prevents jarring jumps when switching between, e.g., pointer tracking and focus following.

**Deadzone (pointer source only):**

```
function applyDeadzone(newPointerPos, lastPointerPos, threshold):
    dx = newPointerPos.x - lastPointerPos.x
    dy = newPointerPos.y - lastPointerPos.y
    distance = sqrt(dx*dx + dy*dy)

    if distance < threshold:
        return lastPointerPos   // Suppress micro-movement
    else:
        return newPointerPos    // Accept movement
```

The threshold scales with display resolution: `threshold = basePx * (screenH / 1080.0)`, where basePx is 3 pixels (tunable).

### 3.5 FocusMonitor

**Purpose:** Subscribes to Windows UI Automation `UIA_AutomationFocusChangedEventId` events and reports the bounding rectangle of the focused element.

**Setup (on UIA thread startup):**

```cpp
IUIAutomation* pAutomation;
CoCreateInstance(CLSID_CUIAutomation, ..., &pAutomation);
pAutomation->AddFocusChangedEventHandler(NULL, pFocusHandler);
```

**Event handler (`IUIAutomationFocusChangedEventHandler::HandleFocusChangedEvent`):**

1. Query the focused `IUIAutomationElement` for its `CurrentBoundingRectangle`.
2. If the rectangle is valid (non-zero, non-degenerate): write it to the shared `focusRectangle` state (via SeqLock). Update `lastFocusChangeTime`.
3. If the rectangle is invalid (element doesn't report bounds): ignore the event silently.

**Debounce:** The debounce logic (AC-2.5.07) is implemented in the ViewportTracker, not here. The FocusMonitor reports every focus change immediately. The ViewportTracker waits 100ms after the most recent change before panning.

### 3.6 CaretMonitor

**Purpose:** Tracks the position of the text cursor (caret) across applications using two complementary techniques.

**Technique 1 — UI Automation TextPattern (preferred):**

When a focus change is reported by FocusMonitor, CaretMonitor checks whether the newly focused element supports the `TextPattern` or `TextPattern2` interface. If so, it subscribes to `TextSelectionChangedEvent` and queries the caret position (the degenerate selection range at the insertion point) to obtain a bounding rectangle.

**Technique 2 — GetGUIThreadInfo polling (fallback):**

Many applications do not implement UIA TextPattern. As a fallback, CaretMonitor polls `GetGUIThreadInfo` at 30Hz on the UIA thread:

```cpp
GUITHREADINFO gti = { sizeof(gti) };
if (GetGUIThreadInfo(0, &gti)) {
    if (gti.flags & GUI_CARETBLINKING) {
        RECT caretRect;
        caretRect.left   = gti.rcCaret.left;
        caretRect.top    = gti.rcCaret.top;
        caretRect.right  = gti.rcCaret.right;
        caretRect.bottom = gti.rcCaret.bottom;
        // Convert from client coordinates to screen coordinates
        POINT topLeft = { caretRect.left, caretRect.top };
        ClientToScreen(gti.hwndCaret, &topLeft);
        // Write to shared state
    }
}
```

**Polling rate:** 30Hz is sufficient because caret position changes are driven by human typing speed (typically 5–15 characters/second). Polling faster would waste CPU. The 30Hz poll is a simple `Sleep(33)` loop on the UIA thread, interleaved with the UIA message pump.

### 3.7 RenderLoop

**Purpose:** The central frame-tick engine. Runs on the Render Thread. Every frame, it computes the final zoom level and viewport offset, then applies them via MagBridge.

**Frame tick pseudocode:**

```
function frameTick():
    // 1. Consume scroll delta (atomic exchange with 0)
    scrollDelta = atomicExchange(sharedScrollAccumulator, 0)
    if scrollDelta != 0:
        zoomController.applyScrollDelta(scrollDelta, currentPointerPos)

    // 2. Process keyboard commands (drain lock-free queue)
    while (cmd = keyboardCommandQueue.tryPop()):
        processCommand(cmd)   // zoom step, toggle, color inversion, etc.

    // 3. Interpolate zoom if animating
    if zoomController.mode == ANIMATING or zoomController.mode == TOGGLING:
        zoomController.currentZoom = interpolate(
            zoomController.currentZoom,
            zoomController.targetZoom,
            animationSpeed
        )
        if abs(zoomController.currentZoom - zoomController.targetZoom) < 0.001:
            zoomController.currentZoom = zoomController.targetZoom
            if zoomController.mode == ANIMATING:
                zoomController.mode = IDLE

    // 4. Compute viewport offset
    source = viewportTracker.determineActiveSource()
    rawTarget = computeTargetForSource(source)

    if source changed since last frame:
        beginSourceTransition(currentOffset, rawTarget, transitionDurationMs)

    if sourceTransitionInProgress:
        viewportOffset = interpolateTransition(currentOffset, rawTarget, elapsed)
    else:
        viewportOffset = rawTarget

    // 5. Apply to magnification API (only if values changed)
    if zoomChanged or offsetChanged:
        magBridge.setTransform(
            zoomController.currentZoom,
            viewportOffset.x,
            viewportOffset.y
        )

    // 6. Update input transform (keeps click coordinates accurate)
    if zoomChanged or offsetChanged:
        magBridge.setInputTransform(
            zoomController.currentZoom,
            viewportOffset.x,
            viewportOffset.y
        )
```

**Interpolation function (ease-out):**

```
function interpolate(current, target, speed):
    // Exponential ease-out: each frame, close a fraction of the remaining gap.
    // 'speed' is a fraction (e.g., 0.15 means close 15% of the gap per frame).
    // At 60fps with speed=0.15, ~90% of the animation completes in ~150ms.
    return current + (target - current) * speed
```

This approach is frame-rate-independent in feel (faster displays produce smoother animation but similar total duration) and handles mid-animation target changes naturally (the gap simply recalculates each frame toward the new target).

**Frame pacing:**

```
function renderThreadMain():
    while not shutdownRequested:
        frameTick()
        DwmFlush()     // Block until next VSync
```

`DwmFlush()` synchronizes the render loop to the display's vertical refresh. Each `frameTick()` executes once per display frame, ensuring both optimal timing and zero wasted computation.

### 3.8 MagBridge

**Purpose:** Encapsulates all interaction with the Windows Magnification API (`Magnification.dll`). No other component calls Magnification API functions directly. This isolation enables future migration to Desktop Duplication API or `Windows.Graphics.Capture` without touching any other component.

**API calls wrapped:**

| Function | When Called | Purpose |
|----------|-----------|---------|
| `MagInitialize()` | Application startup (main thread) | Initialize the magnification runtime. Must succeed or the app exits. |
| `MagUninitialize()` | Application shutdown | Clean up. |
| `MagSetFullscreenTransform(level, xOff, yOff)` | Every render frame where zoom or offset changed | Apply magnification level and viewport position. |
| `MagGetFullscreenTransform(...)` | Startup (to check for existing magnifier) | Query current state to detect conflicts. |
| `MagSetFullscreenColorEffect(pMatrix)` | When color inversion is toggled | Apply or remove the 5×5 inversion matrix. |
| `MagSetInputTransform(enabled, srcRect, destRect)` | Every render frame where zoom or offset changed | Map input coordinates so clicks hit the correct elements. |
| `MagShowSystemCursor(show)` | Startup | Ensure the system cursor remains visible while magnified. |

**Color inversion matrix:**

```
// Identity matrix (no color effect):
// { 1,0,0,0,0,  0,1,0,0,0,  0,0,1,0,0,  0,0,0,1,0,  0,0,0,0,1 }

// Inversion matrix:
float invertMatrix[5][5] = {
    { -1,  0,  0,  0,  0 },
    {  0, -1,  0,  0,  0 },
    {  0,  0, -1,  0,  0 },
    {  0,  0,  0,  1,  0 },
    {  1,  1,  1,  0,  1 }
};
// Row 5 (translation) shifts RGB by +1 to invert: new_r = -r + 1 = 1-r
```

**Input transform calculation:**

```
function setInputTransform(zoom, xOff, yOff):
    // Source rect: the portion of the desktop being displayed
    srcRect = {
        left   = xOff,
        top    = yOff,
        right  = xOff + screenWidth / zoom,
        bottom = yOff + screenHeight / zoom
    }
    // Dest rect: the full screen
    destRect = {
        left   = 0,
        top    = 0,
        right  = screenWidth,
        bottom = screenHeight
    }
    MagSetInputTransform(TRUE, &srcRect, &destRect)
```

**Error handling:** If any Magnification API call fails (returns `FALSE`), MagBridge logs the error via `GetLastError()` and sets an internal error state. The RenderLoop checks this state and, on persistent failure, displays a tray notification and falls back to a degraded state (zoom remains at its last successful level).

**Shutdown sequence:** On application exit, MagBridge must:
1. Set the transform back to (1.0, 0, 0) — unmagnified.
2. Disable the input transform.
3. Remove any color effect (apply identity matrix).
4. Call `MagUninitialize()`.

If the application crashes without running this sequence, the screen may be left magnified. The process should install an unhandled exception handler and a `SetConsoleCtrlHandler` to attempt cleanup on crash.

### 3.9 SettingsManager

**Purpose:** Loads, validates, saves, and distributes configuration settings. Provides a thread-safe snapshot mechanism so that other components never see partially-updated settings.

**Storage:** `%AppData%\SmoothZoom\config.json`.

**Snapshot model:** Settings are stored in an immutable struct. When any setting changes, a new struct is allocated, populated, and the pointer is atomically swapped. Reader threads load the pointer atomically and read from the struct freely without locks. The old struct is freed after a safe delay (or via epoch-based reclamation).

```
struct SettingsSnapshot {
    int     modifierKeyVK;          // VK_LWIN, VK_CONTROL, etc.
    int     toggleKeyComboVK[2];    // e.g., {VK_CONTROL, VK_MENU}
    float   minZoom;                // 1.0
    float   maxZoom;                // 10.0
    float   keyboardZoomStep;       // 0.25
    int     animationSpeed;         // 0=slow, 1=normal, 2=fast
    bool    imageSmoothingEnabled;  // true
    bool    startWithWindows;       // false
    bool    startZoomed;            // false
    float   defaultZoomLevel;       // 2.0
    bool    followKeyboardFocus;    // true
    bool    followTextCursor;       // true
    bool    colorInversionEnabled;  // false
};
```

**Validation on load:** Each field is validated against its allowed range (from Scope §2.9). Invalid values are silently replaced with defaults. A log entry records the correction.

**Observer pattern:** Components that need to respond to settings changes (e.g., InputInterceptor needs to know the modifier key, WinKeyManager needs to know if Win is the modifier) register as observers. When the settings pointer is swapped, observers are notified on the main thread. This is a low-frequency event (only when the user changes a setting) so performance is not a concern.

### 3.10 TrayUI

**Purpose:** Provides the system tray icon, right-click context menu, and settings window.

**Implementation:** Standard Win32 tray icon using `Shell_NotifyIcon` with `NIF_ICON | NIF_TIP | NIF_MESSAGE`. The tray message handler responds to `WM_RBUTTONUP` (show context menu) and `WM_LBUTTONDBLCLK` (open settings).

**Settings window:** A lightweight modal dialog or modeless window built with either Win32 dialogs or a minimal WinUI 3 / WPF UI. The window displays all settings from the SettingsSnapshot and writes changes back through SettingsManager. Complexity is intentionally low — this is a single-page settings panel, not a multi-tab wizard.

---

## 4. Data Flow Diagrams

### 4.1 Scroll-Gesture Zoom (Primary Path)

```
User holds Win + scrolls mouse wheel
         │
         ▼
 ┌─────────────────────┐
 │  InputInterceptor    │  WH_MOUSE_LL callback
 │  (Main Thread)       │  Detects: modifier held + WM_MOUSEWHEEL
 │                      │  Atomically adds delta to scroll accumulator
 │                      │  Sets winKeyUsedForZoom = true
 │                      │  Returns 1 (event consumed)
 └──────────┬──────────┘
            │  (scroll accumulator in shared state)
            ▼
 ┌─────────────────────┐
 │  RenderLoop          │  Next frame tick
 │  (Render Thread)     │  Reads and resets scroll accumulator
 │                      │  Calls ZoomController.applyScrollDelta()
 │                      │  Calls ViewportTracker.computePointerOffset()
 │                      │  Calls MagBridge.setTransform(zoom, x, y)
 └──────────┬──────────┘
            │
            ▼
 ┌─────────────────────┐
 │  MagBridge           │  MagSetFullscreenTransform(zoom, x, y)
 │  (Render Thread)     │  MagSetInputTransform(...)
 └─────────────────────┘
            │
            ▼
       Screen updates on next VSync
```

### 4.2 Keyboard-Shortcut Zoom

```
User presses Win+Plus
         │
         ▼
 ┌─────────────────────┐
 │  InputInterceptor    │  WH_KEYBOARD_LL callback
 │  (Main Thread)       │  Detects: Win held + VK_OEM_PLUS
 │                      │  Posts ZOOM_IN command to keyboard queue
 └──────────┬──────────┘
            │  (command in lock-free queue)
            ▼
 ┌─────────────────────┐
 │  RenderLoop          │  Next frame tick
 │  (Render Thread)     │  Drains keyboard queue
 │                      │  Calls ZoomController.applyKeyboardStep(+1)
 │                      │  ZoomController.mode = ANIMATING
 │                      │
 │                      │  Subsequent frames: interpolates currentZoom
 │                      │  toward targetZoom (ease-out)
 │                      │  Calls MagBridge.setTransform() each frame
 └─────────────────────┘
```

### 4.3 Focus Change

```
User presses Tab → focus moves to off-viewport button
         │
         ▼
 ┌─────────────────────┐
 │  FocusMonitor        │  UIA focus-changed event fires
 │  (UIA Thread)        │  Reads element bounding rectangle
 │                      │  Writes to shared focusRectangle (SeqLock)
 │                      │  Updates lastFocusChangeTime
 └──────────┬──────────┘
            │
            ▼
 ┌─────────────────────┐
 │  RenderLoop          │  Frame tick after debounce (100ms)
 │  (Render Thread)     │  ViewportTracker.determineActiveSource() → FOCUS
 │                      │  computeElementOffset(focusRect, zoom, ...)
 │                      │  Begins smooth transition to new offset
 │                      │  Calls MagBridge.setTransform() each frame
 └─────────────────────┘
```

### 4.4 Temporary Toggle

```
User holds Ctrl+Alt (toggle keys)
         │
         ▼
 ┌─────────────────────┐
 │  InputInterceptor    │  Detects toggle-down
 │  (Main Thread)       │  Posts TOGGLE_ENGAGE command
 └──────────┬──────────┘
            │
            ▼
 ┌─────────────────────┐
 │  RenderLoop          │  Drains queue
 │  (Render Thread)     │  ZoomController: save current zoom, set target=1.0
 │                      │  mode = TOGGLING
 │                      │  Animates currentZoom toward 1.0 over ~250ms
 └─────────────────────┘
            │
       User releases Ctrl+Alt
            │
            ▼
 ┌─────────────────────┐
 │  InputInterceptor    │  Detects toggle-up
 │  (Main Thread)       │  Posts TOGGLE_RELEASE command
 └──────────┬──────────┘
            │
            ▼
 ┌─────────────────────┐
 │  RenderLoop          │  Drains queue
 │  (Render Thread)     │  ZoomController: set target = savedZoom
 │                      │  Animates currentZoom back to savedZoom
 │                      │  On completion: mode = IDLE
 └─────────────────────┘
```

---

## 5. Build and Deployment

### 5.1 Language and Toolchain

| Item | Choice | Rationale |
|------|--------|-----------|
| Language | C++ (C++17 or later) | Direct access to Win32 APIs, Magnification API, and COM (for UIA) without marshaling overhead. No runtime dependency. |
| Compiler | MSVC (Visual Studio 2022+) | Required for Windows SDK integration and `/link Magnification.lib`. |
| Build system | CMake or MSBuild (Visual Studio solution) | Standard for Windows C++ projects. |
| Architecture | x64 only | Magnification API not supported under WOW64. ARM64 optional for Windows on ARM. |

### 5.2 Dependencies

| Dependency | Type | Purpose |
|-----------|------|---------|
| `Magnification.dll` / `Magnification.lib` | Windows system | Full-screen magnification API |
| `UIAutomationCore.dll` | Windows system | Focus and caret tracking via UIA |
| `Dwmapi.dll` / `Dwmapi.lib` | Windows system | `DwmFlush()` for frame pacing |
| `User32.dll` | Windows system | Hooks, window management, input injection |
| `Shell32.dll` | Windows system | System tray (`Shell_NotifyIcon`) |
| nlohmann/json (or similar) | Third-party header-only | JSON parsing for `config.json`. Can be replaced with manual parsing to avoid external dependencies. |

No frameworks (Qt, Electron, .NET) are required. The application is a lean native Win32 binary.

### 5.3 Application Manifest

The executable must include a manifest with the following entries:

```xml
<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<assembly xmlns="urn:schemas-microsoft-com:asm.v1" manifestVersion="1.0">
  <trustInfo xmlns="urn:schemas-microsoft-com:asm.v3">
    <security>
      <requestedPrivileges>
        <requestedExecutionLevel level="asInvoker" uiAccess="true"/>
      </requestedPrivileges>
    </security>
  </trustInfo>
  <compatibility xmlns="urn:schemas-microsoft-com:compatibility.v1">
    <application>
      <!-- Windows 10 / 11 -->
      <supportedOS Id="{8e0f7a12-bfb3-4fe8-b9a5-48fd50a15a9a}"/>
    </application>
  </compatibility>
  <application xmlns="urn:schemas-microsoft-com:asm.v3">
    <windowsSettings>
      <dpiAwareness xmlns="http://schemas.microsoft.com/SMI/2016/WindowsSettings">
        PerMonitorV2
      </dpiAwareness>
    </windowsSettings>
  </application>
</assembly>
```

**Critical:** `uiAccess="true"` is mandatory for `MagSetFullscreenTransform`. Without it, the call silently fails. This flag requires the binary to be digitally signed and installed in a secure location (`Program Files`, `Windows\System32`, or similar).

**DPI awareness:** `PerMonitorV2` ensures the application receives accurate per-monitor DPI values and screen coordinates. Without this, coordinates may be virtualized by DPI scaling, breaking viewport offset calculations on mixed-DPI multi-monitor setups.

### 5.4 Code Signing

The binary must be signed with a valid Authenticode certificate. For development, a self-signed certificate works if installed in the machine's Trusted Root store. For distribution, an Extended Validation (EV) or standard code-signing certificate from a trusted CA is required.

### 5.5 Installation

The installer (MSIX, Inno Setup, or WiX) must:
1. Install the signed binary to `C:\Program Files\SmoothZoom\` (or equivalent secure folder).
2. Optionally create a Start Menu shortcut.
3. Optionally register for auto-start (if the user enabled "Start with Windows" during install or later in settings).
4. **Not** require administrator elevation for regular use — only for installation to `Program Files`. The application runs at standard user privilege with `uiAccess="true"`.

### 5.6 Minimum OS Requirements

- Windows 10 version 1903 (build 18362) or later.
- `MagSetFullscreenTransform`: available since Windows 8.
- `DwmFlush`: available since Windows Vista.
- Per-Monitor DPI Awareness V2: available since Windows 10 1703, stable since 1903.
- UI Automation `IUIAutomation6` (or `IUIAutomation` base interface): available since Windows 7; V6 since Windows 10 1809.

---

## 6. Future Migration Path

Microsoft recommends the Desktop Duplication API (`IDXGIOutputDuplication`) or `Windows.Graphics.Capture` over the Magnification API for new screen-capture-based applications. While the Magnification API remains functional in Windows 11 and no removal date has been announced, the architecture is designed to facilitate migration.

**MagBridge is the sole abstraction boundary.** If the Magnification API is deprecated or removed:

1. Replace MagBridge's internals with a Desktop Duplication or Graphics.Capture implementation.
2. The new implementation would: capture the desktop framebuffer each frame, apply scaling and color transforms via Direct3D/Direct2D shaders, and render the result to a full-screen overlay window.
3. Input transform (coordinate remapping) would need to be handled manually instead of via `MagSetInputTransform`.
4. No other component changes. ZoomController, ViewportTracker, RenderLoop, InputInterceptor, and all other components communicate with MagBridge through the same interface regardless of the underlying implementation.

This migration would increase complexity and development effort but is architecturally bounded to a single component.

# SmoothZoom — MVP Code Review and Analysis

You are reviewing the codebase for **SmoothZoom**, a native C++17 Win32 desktop application that provides macOS-style full-screen magnification for Windows using the Magnification API. The project is nearing MVP completion.

The application is fully specified across five design documents uploaded to this project. **Read all five before beginning your review.** The document map:

- **Doc 1 — Project Scope:** What's in/out of scope, success criteria, constraints, settings
- **Doc 2 — Behavior Specification:** 139 numbered acceptance criteria (AC-prefixed) — the authority on "how should X behave?"
- **Doc 3 — Technical Architecture:** 10 components across 4 layers, threading model, data flow, build/deploy
- **Doc 4 — Phased Delivery Plan:** Seven phases (0–6), exit criteria per phase, dependency graph
- **Doc 5 — Technical Risks:** 22 risks (R-01 through R-22) with mitigations and contingencies

---

## Your Task

Perform a thorough code review and architectural analysis of the SmoothZoom codebase. The review should cover **correctness, spec compliance, threading safety, performance, and maintainability** — in that priority order.

Read every source file in the repository before writing any findings. Understand the full picture first.

---

## 1. Architecture Conformance

Verify the codebase implements the specified four-layer, ten-component architecture:

**Input Layer:**
- `InputInterceptor` — Global low-level hooks (`WH_MOUSE_LL`, `WH_KEYBOARD_LL`), event classification, routing
- `FocusMonitor` — UIA focus-changed event subscription, bounding rectangle extraction
- `CaretMonitor` — UIA TextPattern (preferred) + `GetGUIThreadInfo` 30Hz polling (fallback)
- `WinKeyManager` — Win key state machine, Start Menu suppression via `SendInput` Ctrl injection

**Logic Layer:**
- `ZoomController` — Zoom state (IDLE, SCROLL_DIRECT, ANIMATING, TOGGLING), logarithmic scroll model, soft-approach bounds clamping, animation targets
- `ViewportTracker` — Proportional offset mapping, edge clamping, deadzone, multi-source priority arbitration (pointer > caret during typing > focus), source transition smoothing
- `RenderLoop` — Frame tick engine on the Render Thread, `DwmFlush` sync, interpolation, calls MagBridge

**Output Layer:**
- `MagBridge` — Sole Magnification API wrapper (`MagSetFullscreenTransform`, `MagSetInputTransform`, `MagSetFullscreenColorEffect`, etc.)

**Support Layer:**
- `SettingsManager` — `config.json` load/validate/save, immutable snapshot with atomic pointer swap, observer notification
- `TrayUI` — System tray icon, context menu, settings window

Check that:
- Component boundaries are clean — each has a single responsibility, no cross-layer shortcuts
- MagBridge is the **only** component that touches `Magnification.dll` APIs
- No component other than InputInterceptor/WinKeyManager directly handles hook callbacks
- UIA calls are confined to FocusMonitor and CaretMonitor on the UIA Thread

---

## 2. Threading Model Compliance

The spec mandates exactly **three threads**. This is the highest-risk area for subtle bugs.

### Main Thread
- Win32 message pump (`GetMessage`/`DispatchMessage`)
- Hook installation and callbacks (must be on a thread with a message pump)
- TrayUI message handling
- `MagInitialize()` called here during startup

**Critical rule:** Hook callbacks must be *minimal and non-blocking*. They read the event, update shared state or post a message, and return immediately. They must never approach the ~300ms system hook timeout (R-05).

### Render Thread
- Tight loop synced to VSync via `DwmFlush()`
- Each frame tick: read shared state → interpolate → call MagBridge
- **MUST NOT** allocate heap memory, acquire mutexes, or perform any blocking operation on the hot path
- Skips redundant API calls when zoom/offset haven't changed
- At 1.0× with no animation active, should skip API calls entirely (AC-2.3.13)

### UIA Thread
- Hosts FocusMonitor and CaretMonitor
- Own COM-initialized message pump
- Isolated so slow UIA providers (Java Access Bridge, some Electron apps) cannot drop frames or delay hooks

### Thread Communication (verify each)

| Shared State | Writer | Reader | Required Protection |
|---|---|---|---|
| Modifier key state | Main (hook) | Main (hook) | Atomic |
| Pointer position (x, y) | Main (hook) | Render | Atomic pair or SeqLock |
| Scroll delta accumulator | Main (hook) | Render | Atomic (exchange-with-zero) |
| Keyboard shortcut commands | Main (hook) | Render | Lock-free queue |
| Toggle state | Main (hook) | Render | Atomic |
| Focus target rectangle | UIA | Render | SeqLock |
| Caret target rectangle | UIA | Render | SeqLock |
| Last keyboard input timestamp | Main (hook) | Render | Atomic |
| Last focus change timestamp | UIA | Render | Atomic |
| Settings snapshot | Main (SettingsManager) | All | Copy-on-write (atomic pointer swap) |

Flag any case where:
- The render thread acquires a mutex or allocates memory
- A SeqLock is missing or incorrectly implemented (writer must increment sequence before and after write; reader must retry on mismatch or odd sequence)
- Atomic operations use incorrect memory orderings (e.g., `relaxed` where `acquire`/`release` is needed)
- The lock-free queue has ABA problems or missing memory barriers
- Any thread accesses shared state without the specified protection

---

## 3. Acceptance Criteria Spot-Check

You don't need to verify all 139 ACs, but **spot-check the following high-value behaviors** against the code. For each, note whether the implementation matches the spec, deviates, or is missing.

### Scroll-Gesture Zoom (Phase 1)
- **AC-2.1.02:** With Win released, scroll events pass through with zero modification/delay
- **AC-2.1.06:** Logarithmic (multiplicative) zoom model — each scroll unit multiplies/divides by a constant factor, so 1×→2× takes the same scroll effort as 5×→10×
- **AC-2.1.14/15:** Zoom bound at 10.0× with soft-approach deceleration, not a hard stop
- **AC-2.1.16/17/18:** Start Menu suppression — no Start Menu after Win+Scroll, but Start Menu works on plain Win tap, and Win+E still opens Explorer

### Animation (Phase 2)
- **AC-2.2.06:** Rapid `Win+Plus` presses retarget into a single continuous animation, not sequential animations
- **AC-2.2.07:** `Win+Plus` then immediate `Win+Minus` smoothly reverses with no bounce/stutter
- **AC-2.2.08:** Scroll during keyboard animation: scroll takes over immediately, animation is abandoned

### Accessibility Tracking (Phase 3)
- **AC-2.5.07/09:** Rapid Tab cycling: viewport debounces (100ms) and pans to the *final* focused control, not each intermediate one
- **AC-2.6.07/08:** Caret priority over pointer during typing; smooth handoff back to pointer after 500ms idle

### Temporary Toggle (Phase 4)
- **AC-2.7.08:** After toggle round-trip, restores exact pre-toggle zoom (e.g., 6.3×, not rounded)
- **AC-2.7.09:** Scroll during toggle updates the restore target to the new level
- **AC-2.7.10:** Toggle during a mid-animation captures the mid-animation zoom level

---

## 4. Risk Mitigations Implemented

Verify the codebase addresses the **top-priority risks**. For each, note whether the mitigation is present, partial, or absent.

| Risk | What to Look For |
|---|---|
| **R-04** (Input transform desync) | `MagSetInputTransform` called in the same frame tick, same code block, same values as `MagSetFullscreenTransform` |
| **R-05** (Hook deregistration) | Watchdog timer on main thread that checks hook handles and re-installs if invalid; hook callbacks contain zero blocking operations |
| **R-06** (Start Menu suppression) | `SendInput` Ctrl key-down/up injection on Win release when `winKeyUsedForZoom` is true |
| **R-11** (UIA callback latency) | Timeout on `CurrentBoundingRectangle` queries (~100ms); logging for slow callbacks (>50ms) |
| **R-14** (Crash leaving screen magnified) | Structured exception handler / `SetUnhandledExceptionFilter` that calls `MagSetFullscreenTransform(1.0, 0, 0)` before exit |
| **R-17** (Floating-point accumulation) | Zoom stored as a target value, not accumulated from deltas; no drift over time |
| **R-18** (High CPU at high refresh) | Render loop skips API calls when zoom/offset unchanged; fully idle at 1.0× with no active animation |

---

## 5. Code Quality and Maintainability

- **Build system:** Should target x64 only, MSVC, C++17. Application manifest must include `uiAccess="true"` and `PerMonitorV2` DPI awareness.
- **Error handling:** `MagSetFullscreenTransform` returns `FALSE` on failure — check return values are handled. `MagInitialize()` failure should produce a user-visible error and graceful exit (AC-ERR.02).
- **Naming and structure:** Do component files map cleanly to the ten named components? Are responsibilities leaking across boundaries?
- **Memory:** No `new`/`malloc` on the render thread hot path. Settings use the immutable-snapshot-with-atomic-swap pattern, not locked reads.
- **Logging:** Useful for debugging (hook re-registration, slow UIA callbacks, settings validation corrections) without impacting hot-path performance.

---

## 6. Deliverable

Produce a structured report with:

1. **Executive summary** — Overall health assessment, top 3 concerns, top 3 strengths
2. **Architecture conformance** — Component-by-component checklist against the spec
3. **Threading safety findings** — Every issue found, rated by severity (critical / major / minor)
4. **Spec compliance** — Results of the AC spot-checks, with code references
5. **Risk mitigation status** — Table showing each risk and its implementation status
6. **Code quality observations** — Style, structure, error handling, build configuration
7. **Recommended actions** — Prioritized list of changes needed before release, grouped into "must fix," "should fix," and "consider"

Reference specific AC numbers (e.g., AC-2.1.06), risk IDs (e.g., R-05), and file/line locations throughout.

# SmoothZoom — Project-Wide Constraints

## Current Phase: Phase 6 — Polish & Hardening (code complete, verification in progress)

Phases 0–5 are delivered. All ten components exist and are wired: scroll-gesture zoom,
keyboard shortcuts with animation, UIA focus/caret tracking, temporary toggle, settings +
tray UI, color inversion, multi-monitor support, conflict detection, crash recovery, and
the hook watchdog are all implemented. **CLAUDE.md is the single source of truth for
status** — if this file and CLAUDE.md ever disagree, trust CLAUDE.md and fix this file.

Remaining Phase 6 work is verification and hardening, not new features:
- Manual AC verification (E6.13) — `docs/manual_test_checklist_v2.md` tracks it
- Performance targets (E6.12) — CPU idle/active currently exceed targets (R-18)
- Bug-fix passes from review findings

**Do not remove or re-scope shipped features.** The phase-gating table that previously
lived here (forbidding Phase 2+ features) is obsolete — everything through Phase 6 is
implemented and must not be regressed. Known intentional deferrals: AC-2.3.08/09
(nearest-neighbor / smoothing toggle, blocked by the Magnification API) and AC-MM.01's
"other monitors stay 1.0×" clause (global transform limitation) — both deferred to the
Desktop Duplication migration (R-01).

## Architecture — Ten Components, Four Layers

```
Input:   InputInterceptor · WinKeyManager · FocusMonitor · CaretMonitor
Logic:   ZoomController · ViewportTracker · RenderLoop
Output:  MagBridge
Support: SettingsManager · TrayUI
```

Each component has a single responsibility. Do not merge components or move
responsibilities across boundaries. If a new behavior doesn't fit an existing component,
that's a design conversation, not a reason to stuff it into the nearest file.
(Known debt: Raw Input / PTP touchpad handling currently lives in `main.cpp` and is
slated to move into the Input layer — see `docs/input-interop-handoff.md` item #6.)

## Threading Model — Four Threads, Strict Affinity

| Thread | Owns | Key Constraint |
|--------|------|----------------|
| **Main** | Message pump, hook callbacks (WH_MOUSE_LL, WH_KEYBOARD_LL), watchdog timer, TrayUI, Raw Input/PTP | Hook callbacks must be minimal: read struct, atomic write, return. No computation, no I/O, no COM, no blocking. |
| **Render** | RenderLoop frame tick, ALL MagBridge calls (init through shutdown — Mag API thread affinity) | No heap allocation, no mutex, no I/O, no blocking calls except DwmFlush(). Must pump PeekMessage/DispatchMessage each frame or transform offsets are silently ignored. |
| **UIA** | FocusMonitor, COM/UIA event subscriptions | Isolated so slow UIA providers cannot stall hooks or drop frames. Bounding-rect queries bounded by IUIAutomation6 ConnectionTimeout (100ms, R-11). |
| **Caret** | CaretMonitor GTTI polling (~30Hz) | Separate from UIA thread: UIA caret events are unreliable, and polling must not block the event-driven FocusMonitor. (Documented deviation from Doc 3's three-thread model.) |

**Thread communication uses only:** atomics, SeqLock (small structs), lock-free queue
(commands), copy-on-write with atomic pointer swap (settings). No `std::mutex`, no
`CRITICAL_SECTION`, no `WaitForSingleObject` on any hot path.

## Cross-Cutting Invariants

1. **No `#include <Magnification.h>` outside MagBridge.** All Mag* calls go through
   MagBridge's public interface. This isolation bounds a future Desktop Duplication
   migration to one file. (R-01) *Documented exception:* `main.cpp`'s crash handler
   calls Mag* directly because MagBridge cannot be safely constructed inside an
   exception filter — keep that exception confined to `crashHandler()`.

2. **No heap allocation on hot paths.** This applies to both hook callbacks (Main
   thread) and `frameTick()` (Render thread). No `new`, `malloc`,
   `std::vector::push_back`, `std::string` construction, or STL container mutation.

3. **No per-frame / per-event logging.** Log only on state transitions (zoom
   started/ended, source changed, error detected, hook re-registered). At 144Hz,
   per-frame logging is 144 writes/second — and the logger does a synchronous file
   flush per line.

4. **`GetCursorPos()` for pointer position in RenderLoop, not SharedState.** The
   low-level mouse hook's `WM_MOUSEMOVE` is unreliable under fullscreen magnification.
   `GetCursorPos()` is a fast shared-memory read (~1µs) that always returns the true
   position.

5. **`MagSetInputTransform` is NOT called per-frame.** With proportional mapping
   (`offset = pos * (1 - 1/zoom)`), input transform is mathematically redundant and
   causes a feedback loop with `GetCursorPos()`. Only `MagSetFullscreenTransform` is
   called per-frame. `MagSetInputTransform(FALSE)` is called only during shutdown and
   in the crash handler.

6. **Use `double` for internal zoom math, `float` only at the API boundary.** Prevents
   floating-point accumulation drift over long sessions. Snap to exactly 1.0 when
   within epsilon (0.005). (R-17)

7. **Integer division in viewport math is a bug.** `screenW / zoom` must be
   floating-point division. Integer truncation causes click-offset errors that worsen
   at higher zoom levels.

8. **One clock domain for arbitration timestamps.** ViewportTracker source arbitration
   compares `lastPointerMoveTime` / `lastFocusChangeTime` / `lastKeyboardInputTime` /
   `lastCaretUpdateTime` — all of these are steady_clock milliseconds. Never store a
   `KBDLLHOOKSTRUCT::time` / `GetTickCount` value into them: the domains diverge across
   suspend/resume and the DWORD wraps at 49.7 days. (GetTickCount64 is fine for values
   compared only against other GetTickCount64 values, e.g. `lastLLHookScrollTime` and
   the hook liveness stamp.)

9. **Validate every UIA/GTTI rectangle, and gate the caret on freshness.** Reject
   zero-area, negative, and absurd rects (R-09). The caret rect is additionally gated
   on `lastCaretUpdateTime` recency in RenderLoop — CaretMonitor only writes on
   successful polls, so without the freshness gate a stale rect from a closed window
   hijacks arbitration on every keystroke.

## Build Constraints

- **C++17 / MSVC / x64 only.** No external frameworks, no Boost, no Qt. Win32 API + STL
  only. nlohmann/json (header-only, vendored in third_party/) is the sole exception;
  Catch2 is fetched by CMake for tests only.
- **UIAccess = true** in the application manifest, PerMonitorV2 DPI awareness. Binary
  must be signed and run from a secure folder (`C:\Program Files\SmoothZoom\`).
  Without all three, `MagSetFullscreenTransform` silently returns FALSE. (R-12)
- **Build:** `scripts\build.bat` or CMake directly. Sign: `scripts\sign-binary.ps1`.
  Install: `scripts\install-secure.ps1`.
- **Unit tests** cover pure logic (ZoomController, ViewportTracker, WinKeyManager,
  SettingsManager, ScrollNormalizer, ModifierUtils) with no Win32 API dependencies.
  Run via `ctest -C Debug` from the build directory.

---
paths:
  - "**/MagBridge*"
---
# MagBridge — Magnification API Wrapper

## RULE: API Isolation Boundary

**No file other than MagBridge.cpp/h may `#include <Magnification.h>` or call any `Mag*` function.** This is absolute. If you are writing code in any other component that needs magnification behavior, it must go through MagBridge's public interface. This isolation bounds a future Desktop Duplication API migration to this single file. (R-01)

## Threading Constraints

Each Mag* function has a required calling thread. Violating these causes undefined behavior or silent failure.

| Function | Thread | Notes |
|----------|--------|-------|
| `MagInitialize()` | Main | Must succeed or app exits (AC-ERR.02) |
| `MagUninitialize()` | Main | Also called from crash handler |
| `MagSetFullscreenTransform` | Render | Every frame where zoom/offset changed |
| `MagSetInputTransform` | Render | Same frame tick as SetFullscreenTransform |
| `MagGetFullscreenTransform` | Main | Startup conflict detection only |
| `MagShowSystemCursor(TRUE)` | Main | Startup only |

No other Mag* calls should exist anywhere in the codebase.

## Critical: Input Transform Sync (R-04)

**Always call `MagSetInputTransform` in the same frame tick, same code block as `MagSetFullscreenTransform`, using identical zoom and offset values.** Never read values separately. Pass as a single parameter set. Desync causes clicks to miss targets — at 5× zoom, a 1px error = 5px miss.

```
// Input transform rectangles:
srcRect  = { xOff, yOff, xOff + screenW/zoom, yOff + screenH/zoom }
destRect = { 0, 0, screenW, screenH }
MagSetInputTransform(TRUE, &srcRect, &destRect)
```

## Error Handling

- Every Mag* call that returns `FALSE`: log via `GetLastError()`, set internal error flag.
- RenderLoop checks error flag. On persistent failure: tray notification, fall back to last-known-good zoom level.
- Never silently swallow a failure — the API fails silently enough already (UIAccess issues return FALSE with no explanation).

## Shutdown Sequence — Order Matters

```
1. MagSetFullscreenTransform(1.0f, 0, 0)   // Reset to unmagnified
2. MagSetInputTransform(FALSE, ...)          // Disable input mapping
3. MagSetFullscreenColorEffect(identity)     // Remove color inversion
4. MagUninitialize()
```

## Crash Recovery (R-14)

- On init: write a sentinel file (`%AppData%\SmoothZoom\.running`). Remove on clean shutdown.
- On next startup: if sentinel exists, immediately call `MagSetFullscreenTransform(1.0, 0, 0)` before anything else.
- The main thread installs `SetUnhandledExceptionFilter` that calls the shutdown sequence. MagBridge exposes a `resetAndCleanup()` method for this.

---

## ⛔ Phase 6 Only — Do NOT Implement Until Phase 6

Everything below this line covers Phase 6 features. Do not implement any of this during Phases 0–5.

### Color Inversion (Phase 6 — AC-2.10.01 through AC-2.10.05)

Add `MagSetFullscreenColorEffect(pMatrix)` call — Render thread, on color inversion toggle.

```cpp
// Inversion: new_channel = 1 - old_channel
float invertMatrix[5][5] = {
    { -1,  0,  0,  0,  0 },
    {  0, -1,  0,  0,  0 },
    {  0,  0, -1,  0,  0 },
    {  0,  0,  0,  1,  0 },  // Alpha unchanged
    {  1,  1,  1,  0,  1 }   // Translation: +1 to RGB
};
// Identity matrix (no effect): diagonal of 1s
```

### Conflict Detection (Phase 6 — AC-ERR.01, AC-ERR.02)

- Before `MagInitialize`: check for `Magnify.exe` process. If found, prompt user.
- After `MagInitialize`: call `MagGetFullscreenTransform`. If zoom ≠ 1.0, another magnifier is active — warn user.

### Multi-Monitor (Phase 6 — AC-MM.01 through AC-MM.04)

- Use `MonitorFromPoint` to detect active monitor.
- On monitor change: reapply transform for new monitor's geometry.
- On `WM_DISPLAYCHANGE` / `WM_DPICHANGED`: re-query screen dimensions, adjust input transform rectangles. (AC-ERR.05)

---

## Common Mistakes

These are specific errors to watch for when writing or reviewing MagBridge code.

1. **Calling `MagSetInputTransform` in a separate function from `MagSetFullscreenTransform`.** These must be in the same code block with the same values. Do not factor them into separate helpers that could be called independently.

2. **Including `<Magnification.h>` in another component.** If you need magnification behavior elsewhere, add a method to MagBridge's public interface. Never leak Mag* calls outside this file.

3. **Calling `MagUninitialize()` before resetting the transform.** Follow the shutdown sequence exactly. Uninitializing first can leave the screen stuck at the last zoom level.

4. **Forgetting `GetLastError()` after a failed Mag* call.** The API returns FALSE for many different failure modes (missing UIAccess, another magnifier active, invalid parameters). Without `GetLastError()`, failures are undiagnosable.

5. **Computing `srcRect` with integer division.** `screenW/zoom` must be floating-point division. Integer truncation causes the input transform to drift from the display transform, creating click offset errors that worsen at higher zoom levels.

6. **Calling Mag* functions from the wrong thread.** `MagSetFullscreenTransform` and `MagSetInputTransform` must be called from the Render thread. `MagInitialize` and `MagUninitialize` must be called from the Main thread. Cross-thread calls cause silent failures or deadlocks.

7. **Adding Phase 6 features (color inversion, conflict detection, multi-monitor) before Phase 6.** These are gated. Check the phase plan before implementing.

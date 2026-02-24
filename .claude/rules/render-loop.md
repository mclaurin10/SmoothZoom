---
paths:
  - "**/RenderLoop*"
---
# RenderLoop — Frame Tick Engine

Runs on the **Render Thread**. The central coordination point: every frame, it reads shared state, computes interpolated values, and makes the single MagBridge call that updates the display.

## Hot Path Invariants

These rules are absolute. Violating any of them causes frame drops or worse. **Before adding any code inside `frameTick()`, verify it does not violate any of these four invariants.**

1. **No heap allocation.** Every per-frame value comes from pre-allocated shared state. No `new`, `malloc`, `std::vector::push_back`, `std::string` construction, or any STL container mutation on the hot path.
2. **No mutex acquisition.** Use atomics and SeqLock only. No `std::mutex`, `std::lock_guard`, `CRITICAL_SECTION`, or `WaitForSingleObject`.
3. **No I/O.** No file access, no logging on the per-frame path. Log only on state transitions or errors.
4. **No blocking calls** other than `DwmFlush()` (which is the intentional frame-pace block).

## Frame Tick Structure

```
frameTick():
    1. Consume scroll delta      — atomicExchange(scrollAccumulator, 0)
    2. Drain keyboard commands   — lock-free queue tryPop() loop
    3. Apply scroll to zoom      — ZoomController.applyScrollDelta() if delta ≠ 0
    4. Interpolate zoom          — if ANIMATING or TOGGLING mode, ease-out toward target
    5. Compute viewport offset   — GetCursorPos() for pointer, SeqLock for focus/caret
    6. Call MagBridge             — only if zoom or offset changed since last frame
```

## Message Pump Requirement

The render thread's frame loop includes a `PeekMessage`/`DispatchMessage` pump between `frameTick()` and `DwmFlush()`. This is **required** for `MagSetFullscreenTransform` viewport offsets to take effect. The Magnification API uses internal DWM messages to apply offsets; without a message pump on the calling thread, offsets are silently ignored while the zoom factor still applies correctly.

```
while not shutdownRequested:
    frameTick()
    PeekMessage / TranslateMessage / DispatchMessage loop
    DwmFlush()
```

**Hot path safety:** `PeekMessage` with no pending messages is ~1µs, no heap allocation, no mutex. The render thread has no windows and no hooks, so unexpected messages are not a concern.

**Note:** `MagSetInputTransform` is intentionally not called per-frame. With proportional mapping (`offset = pos * (1 - 1/zoom)`), `desktop_under_cursor == screen_position`, so clicks land correctly without input remapping. Enabling it causes a feedback loop when combined with `GetCursorPos()` (which returns remapped coords when input transform is active).

## Thread Lifecycle

MagBridge initialization and shutdown happen inside `threadMain()` so all Mag* API calls share the same thread (thread affinity requirement). The main thread spin-waits on `initComplete_` to detect init failure.

```
renderThreadMain():
    MagBridge.initialize()       // Thread affinity: same thread as SetFullscreenTransform
    signal initComplete
    while not shutdownRequested:
        frameTick()
        PeekMessage / DispatchMessage loop   // Required for Mag* offset processing
        DwmFlush()               // Blocks until next VSync
    MagBridge.setTransform(1.0)  // Reset zoom
    MagBridge.shutdown()         // MagUninitialize on same thread as MagInitialize
```

`DwmFlush()` ties the loop to the display's refresh rate. 60Hz → 60 ticks/s. 144Hz → 144 ticks/s.

## Interpolation — Exponential Ease-Out

```
newValue = current + (target - current) * speed
// speed ≈ 0.15 per frame → ~90% complete in ~150ms at 60fps
```

This handles mid-animation retargeting naturally: when `target` changes, the gap recalculates each frame. No special cancellation or queuing logic needed. (AC-2.2.06, AC-2.2.07)

## Optimization: Skip Redundant API Calls

- If zoom and offset are identical to last frame's values, skip `MagBridge.setTransform()`. Only `DwmFlush()` runs.
- At zoom 1.0× with no active animation: skip all computation. The loop is effectively idle. (AC-2.3.13)
- This is critical for meeting the <2% CPU target when zoomed but pointer stationary.

## Performance Targets

- Input-to-display latency: ≤1 frame (≤16ms at 60Hz)
- Per-frame tick cost: <0.1ms
- CPU, zoomed + pointer stationary: <2%
- CPU, zoomed + pointer moving: <5%

## Adaptive Frame Rate (R-18 contingency)

If CPU exceeds targets on 240Hz displays despite optimizations: implement adaptive pacing where the loop runs at a lower rate (e.g., 60Hz) during idle periods, ramping to full VSync only during active input.

---

## ⛔ Phase 3+ Only — Do NOT Implement Before Phase 3

### Source Transition Smoothing (Phase 3+)

When ViewportTracker switches active source (e.g., Pointer → Focus), the offset target jumps. Smooth this with a 200ms transition animation, not an instant snap. The RenderLoop detects source changes and manages the transition interpolation.

---

## Common Mistakes

These are specific errors to watch for when writing or reviewing RenderLoop code.

1. **Reading `scrollAccumulator` with a plain atomic load instead of `exchange`.** The consume step must atomically read AND reset the accumulator to zero. A plain load leaves deltas unconsumed, causing double-application on the next frame.

2. **Allocating `std::string` or `std::vector` inside `frameTick()`.** Even seemingly harmless operations like string formatting for debug output violate invariant #1. If you need debug logging, set a flag and log outside the hot path on a state transition.

3. **Acquiring a mutex to read settings.** Settings use copy-on-write with atomic pointer swap. The render thread reads through the atomic snapshot pointer. Never lock a mutex to read configuration values during `frameTick()`.

4. **Calling `MagSetInputTransform` per-frame alongside `setTransform`.** With proportional viewport mapping, input transform is mathematically redundant and causes a feedback loop with `GetCursorPos()`. The render loop only calls `setTransform()`. `MagBridge::shutdown()` handles disabling input transform on exit.

5. **Forgetting to check `shutdownRequested` before `DwmFlush()`.** `DwmFlush()` blocks until VSync. If shutdown is requested mid-frame, the thread should exit promptly, not wait for the next VSync.

6. **Using `Sleep()` or `WaitableTimer` instead of `DwmFlush()`.** `DwmFlush()` synchronizes to the actual display VSync. Manual timers drift, causing either dropped frames or wasted cycles. Only fall back to manual timing if `DwmFlush()` is unavailable (see Architecture §2.2).

7. **Adding source transition smoothing before Phase 3.** ViewportTracker doesn't support multiple input sources until Phase 3 delivers UIA integration. Source transitions before that phase are dead code.

8. **Logging every frame.** Even a lightweight log call per frame at 144Hz = 144 writes/second. Log only on state transitions (zoom started, zoom ended, source changed, error detected).

9. **Not skipping the MagBridge call when values haven't changed.** At idle (zoomed, pointer stationary), the frame tick should cost nearly zero. Redundant API calls waste CPU and may add latency.

10. **Reading pointer position from SharedState atomics instead of `GetCursorPos()`.** The low-level mouse hook's `WM_MOUSEMOVE` events are not reliably delivered when the fullscreen magnifier is active. `GetCursorPos()` is a fast shared-memory read (~1µs) that always returns the true cursor position.

11. **Not pumping messages on the render thread.** `MagSetFullscreenTransform` offsets are silently ignored if the calling thread has no message pump. The zoom factor applies but the viewport stays anchored at (0,0). The `PeekMessage`/`DispatchMessage` loop after `frameTick()` is not optional — it enables the Magnification API's internal DWM message processing.

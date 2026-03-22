# SmoothZoom — Post-Review Fix Pass

You are implementing fixes for **SmoothZoom** based on a completed code review. The project is a native C++17 Win32 desktop application that provides macOS-style full-screen magnification using the Magnification API. Five design documents are uploaded to this project — reference them for architectural context.

The code review identified two **must-fix** issues and five **minor fixes** that share file context with them. This prompt covers all seven in a single pass to minimize churn across the affected files.

Read the full codebase before making changes. Understand the threading model and data flow first.

---

## Fix 1 — Crash Handler Thread Affinity (Must Fix)

**Finding:** `main.cpp:210-213` — The `SetUnhandledExceptionFilter` callback creates a new `MagBridge` instance and calls `MagInitialize()` from whatever thread the crash occurred on. The Magnification API has thread affinity requirements — `MagInitialize` and `MagSetFullscreenTransform` should be called from the thread that originally initialized the magnification runtime. If the crash occurs on the render thread or UIA thread, the zoom reset silently fails, leaving the user's screen stuck at a magnified level (the R-14 nightmare scenario).

**Current defense layers (preserve all of these):**
- `SetUnhandledExceptionFilter` crash handler
- Sentinel file (written on startup, removed on clean exit, checked on next launch)
- `WM_ENDSESSION` handler for system shutdown/logoff
- Session lock/unlock notifications

**Required fix:**

Do **not** create a new MagBridge in the crash handler. Instead, call `MagSetFullscreenTransform(1.0f, 0, 0)` directly as a best-effort reset, bypassing the MagBridge abstraction. This is acceptable because:

1. We are already inside an unhandled exception filter — normal safety guarantees do not apply.
2. The Magnification API may or may not honor the call from a non-owning thread, but it costs nothing to try and cannot make things worse.
3. The sentinel file provides the backup recovery path on next launch if this call fails.

Implementation guidance:
- Remove the `MagBridge` construction and `MagInitialize()` call from the crash handler.
- Replace with a direct call to `MagSetFullscreenTransform(1.0f, 0, 0)`. You will need to `#include <magnification.h>` in `main.cpp` (or forward-declare the function) — this is the one acceptable exception to the "only MagBridge includes Magnification.h" rule, and it should be documented with a comment explaining why.
- Follow the `MagSetFullscreenTransform` call with `MagSetInputTransform(FALSE, nullptr, nullptr)` to disable the input transform as well, wrapped in its own try/catch or SEH guard since the process state is already compromised.
- Keep the sentinel file logic and all other recovery layers unchanged.

**Validation:** After this fix, killing the process via Task Manager while zoomed at 5.0× should reset the screen to 1.0× in most crash scenarios. The sentinel file handles the remaining edge cases (e.g., `TerminateProcess`).

---

## Fix 2 — FocusMonitor UIA Bounding Rectangle Timeout (Must Fix)

**Finding:** `FocusMonitor.cpp:89` — `get_CurrentBoundingRectangle()` has no explicit timeout. A hanging UIA provider (Java Access Bridge, some Electron apps) blocks the UIA thread indefinitely. Per the spec (Doc 5, R-11), this call should be bounded to ~100ms, and slow callbacks (>50ms) should be logged.

**Context:** The UIA thread is already isolated from the render thread and main thread (by design), so a stall here cannot drop frames or delay hook processing. But it *does* block all subsequent focus and caret updates until the call returns, meaning accessibility tracking goes dark for the duration.

**Required fix:**

Wrap the `get_CurrentBoundingRectangle()` call with a timeout mechanism. The recommended approach:

**Option A — `std::async` with `wait_for` (simplest, preferred):**
```cpp
auto future = std::async(std::launch::async, [&]() {
    return pElement->get_CurrentBoundingRectangle(&rect);
});
if (future.wait_for(std::chrono::milliseconds(100)) == std::future_status::timeout) {
    // Log: "UIA bounding rect query timed out for element (>100ms)"
    return; // Skip this focus update — viewport simply doesn't pan
}
HRESULT hr = future.get();
```

**Important considerations:**
- If using `std::async`, the launched thread needs COM initialization if it makes COM calls. However, `get_CurrentBoundingRectangle` is being called on a COM interface pointer — the actual cross-process call happens on whatever apartment the proxy is marshaled to. Test whether this works from a non-COM-initialized thread. If not, fall back to Option B.
- **Option B — Dedicated worker with pre-initialized COM:** Spawn a single persistent helper thread (COM-initialized) that processes bounding rect requests from a queue with timeouts. More complex but avoids COM threading issues.
- **Option C — `IUIAutomation::ConnectionTimeout`:** Check if the `IUIAutomation6` interface exposes `ConnectionTimeout` property. If available on the minimum supported OS (Windows 10 1903+), this is the cleanest solution — set it once during initialization and all UIA calls respect it.

Whichever approach you choose:
- Log slow callbacks (>50ms) with the element name or control type if available, for diagnostic purposes.
- On timeout, skip the focus update entirely. Do not retry — the next focus change event will trigger a fresh attempt.
- Do **not** introduce blocking or mutex acquisition that could affect the render thread. The timeout mechanism must be confined to the UIA thread (or its helper).

**Validation:** With a Java application (known slow UIA provider) in the foreground, rapid Tab cycling should not cause SmoothZoom to hang or become unresponsive. Focus tracking may be degraded for that application (expected and acceptable per AC-2.5.14), but pointer tracking and zoom must remain fully functional.

---

## Fix 3 — MagBridge Screen Dimension Race (Minor)

**Finding:** `MagBridge.cpp:26-27` — `s_screenWidth` and `s_screenHeight` are non-atomic `static int`, written by the main thread on `WM_DISPLAYCHANGE` and read by the render thread in `setInputTransform()`. This is a data race under the C++ memory model.

**Fix:** Change both to `std::atomic<int>` with relaxed ordering. Reads use `load(std::memory_order_relaxed)`, writes use `store(value, std::memory_order_relaxed)`. Relaxed is sufficient because the worst case is a single frame with stale dimensions during a display configuration change — there is no correctness dependency on ordering with other shared state.

---

## Fix 4 — Remove Dead SharedState Fields (Minor)

**Finding:** `SharedState.h:23-24` — `pointerX` and `pointerY` are atomic fields written by the mouse hook callback in `InputInterceptor.cpp` but never read by any consumer. The render thread uses `GetCursorPos()` directly for fresher pointer data.

**Fix:**
1. Remove `pointerX` and `pointerY` from `SharedState`.
2. Remove the corresponding atomic writes in `InputInterceptor.cpp`'s mouse hook callback.
3. Verify no other file references these fields (search the codebase).

This eliminates two wasted atomic stores on every `WM_MOUSEMOVE` in the hook callback — a minor but free performance improvement on the most frequently executed code path.

---

## Fix 5 — Document SeqLock Platform Assumption (Minor)

**Finding:** `SeqLock.h:34` — `data_` is non-atomic. Concurrent read/write on a non-atomic variable is undefined behavior under the C++ standard, even though the reader discards results on sequence mismatch. This is safe on x86-64/MSVC (total store order, no word tearing for naturally aligned types) but not portable.

**Fix:** Add a documentation comment at the top of `SeqLock.h`:

```cpp
// PLATFORM ASSUMPTION: This SeqLock implementation relies on x86-64 total store
// order and MSVC's guarantee of no word-tearing for naturally aligned reads/writes
// up to 8 bytes. The non-atomic read of data_ during a concurrent write is
// technically undefined behavior per the C++ standard (ISO 14882 §6.9.2.1), but
// the retry-on-sequence-mismatch protocol ensures torn values are always discarded.
//
// For portability to ARM or other compilers, replace the raw read/write of data_
// with memcpy through char* (permitted aliasing) or std::atomic<T> with
// std::memory_order_relaxed (if T is lock-free at the required size).
```

No functional change. This prevents a future contributor from "fixing" the SeqLock in a way that introduces performance regression (e.g., adding a mutex) or from porting it to ARM without understanding the assumption.

---

## Fix 6 — Document Four-Thread Deviation (Minor)

**Finding:** The spec (Doc 3, §2) mandates three threads. The implementation uses four — CaretMonitor runs its own `std::thread` for 30Hz GTTI polling, separate from FocusMonitor's UIA thread. This is a defensible design decision but is undocumented.

**Fix:** Add a comment near the CaretMonitor thread launch explaining the rationale:

```cpp
// DEVIATION FROM SPEC (Doc 3, §2): The spec prescribes three threads (Main, Render,
// UIA). CaretMonitor runs on a fourth thread because:
// 1. GTTI polling (GetGUIThreadInfo) does not require COM initialization.
// 2. Isolating it from the UIA thread prevents slow UIA providers (R-11) from
//    blocking caret updates — a stalled get_CurrentBoundingRectangle() call on the
//    UIA thread would otherwise freeze caret tracking too.
// 3. The thread is lightweight (30Hz Sleep loop) with negligible resource cost.
```

No functional change.

---

## Fix 7 — Remove Dead Logger Macro (Minor)

**Finding:** `Logger.h:202` — `SZ_LOG_IMPL_` macro is defined but never referenced anywhere in the codebase.

**Fix:** Delete the unused macro definition.

---

## Execution Order

Work through the fixes in this order to minimize context switching across files:

1. **Fix 1** (crash handler) — touches `main.cpp`, possibly `MagBridge.h` for the forward declaration
2. **Fix 3** (screen dimension atomics) — touches `MagBridge.cpp`, already open from Fix 1's context
3. **Fix 2** (FocusMonitor timeout) — touches `FocusMonitor.cpp`, self-contained
4. **Fix 4** (dead SharedState fields) — touches `SharedState.h`, `InputInterceptor.cpp`
5. **Fix 5** (SeqLock comment) — touches `SeqLock.h`, adjacent to SharedState work
6. **Fix 6** (CaretMonitor comment) — touches `CaretMonitor.cpp`, adjacent to UIA work from Fix 2
7. **Fix 7** (dead macro) — touches `Logger.h`, quick final cleanup

After all fixes, do a full build to verify no regressions. Run the existing test suite if available.

---

## What NOT to Change

- **Do not** touch the `WM_ENDSESSION` handler, sentinel file logic, or session lock/unlock handling — these are correct and must remain as-is.
- **Do not** refactor the SeqLock to use mutexes or `std::atomic<T>` — the current implementation is correct on the target platform. Only add the documentation comment.
- **Do not** change `std::atomic_load/store` on `shared_ptr` to `std::atomic<shared_ptr>` — that requires C++20 and is a future migration item.
- **Do not** change the settings save pattern to write-to-temp-then-rename — that's a separate improvement with its own design surface.
- **Do not** introduce any new threads beyond the existing four.

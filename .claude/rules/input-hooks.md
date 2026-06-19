---
paths:
  - "**/InputInterceptor*"
  - "**/WinKeyManager*"
---
# InputInterceptor & WinKeyManager — Hook Callback Rules

## Hook Callback Invariants

Hook callbacks execute on the Main thread and are subject to the Windows system timeout (~300ms, configurable via `LowLevelHooksTimeout` registry). **If the callback exceeds this timeout, Windows silently removes the hook.** (R-05, High likelihood / High impact)

Every hook callback must:
1. Read the event struct (`MSLLHOOKSTRUCT` or `KBDLLHOOKSTRUCT` from `lParam`).
2. Perform at most one or two atomic writes to shared state.
3. Return immediately — either `1` (consume) or `CallNextHookEx(...)` (pass through).

**Forbidden inside any hook callback:**
- Heap allocation (`new`, `malloc`, `std::string`, STL container mutation)
- Mutex acquisition or any blocking synchronization
- File I/O, logging, or debug output
- COM calls or cross-apartment marshaling
- `SendMessage` to another thread (use `PostMessage` if absolutely necessary)
- Any computation beyond reading the struct and writing an atomic

## Mouse Hook — Scroll Consumption Rules

```
WH_MOUSE_LL callback:
  WM_MOUSEWHEEL + modifier held → atomic-add delta to scrollAccumulator,
                                   set winKeyUsedForZoom = true,
                                   return 1 (CONSUME)
  WM_MOUSEMOVE                  → write (x, y) to shared state,
                                   return CallNextHookEx (PASS THROUGH)
  Everything else               → return CallNextHookEx (PASS THROUGH)
```

**Only `WM_MOUSEWHEEL` with the modifier held is consumed.** All other mouse events pass through unconditionally. Consuming the wrong event breaks other applications. (AC-2.1.02)

**Do NOT rely on `WM_MOUSEMOVE` from the hook for pointer position in the RenderLoop.** These events are unreliable when the fullscreen magnifier is active. The RenderLoop reads pointer position via `GetCursorPos()` directly. The hook writes to shared state as a secondary signal only.

## Keyboard Hook — Observe Only, Except the Four Zoom Keys

```
WH_KEYBOARD_LL callback:
  Modifier key down/up          → update modifier state, delegate to WinKeyManager
  Shortcut combos (Phase 2+)    → post command to lock-free queue
  All keyboard events           → record timestamp to lastKeyboardInputTime
  Zoom key + modifier held      → return 1 (CONSUME) — VK_OEM_PLUS / VK_ADD /
                                   VK_OEM_MINUS / VK_SUBTRACT, on down AND up
  Everything else               → return CallNextHookEx (PASS THROUGH)
```

**The keyboard hook returns 1 only for the four zoom keys (`VK_OEM_PLUS`, `VK_ADD`, `VK_OEM_MINUS`, `VK_SUBTRACT`), and only while the configured modifier is held — on both key-down and key-up.** This is a deliberate, bounded exception: when the modifier is Shift, `Shift+=`/`Shift+-` would otherwise leak a `+`/`_` character into the focused app, and peripheral macros (Logitech Options+, AHK) synthesize those same chords via `SendInput`. The zoom command is posted on key-down; consuming both edges blocks the character without affecting any other key. Every other keyboard event passes through unchanged (observe-only). WinKeyManager's Start Menu suppression is a separate mechanism — it injects a synthetic keystroke and does not consume the Win key event.

## WinKeyManager — Start Menu Suppression State Machine

```
         Win key down
IDLE ──────────────────► WIN_HELD
  ▲                         │
  │                         ├── Scroll consumed → usedForZoom = true
  │                         ├── Other key pressed (e.g. E) → usedWithOtherKey = true
  │                         │
  │   Win key up            │
  │◄────────────────────────┘
  │
  │   On Win key up:
  │     if usedForZoom AND NOT usedWithOtherKey:
  │         inject Ctrl down+up via SendInput → suppresses Start Menu
  │     else:
  │         pass through normally (Start Menu opens or Win+key shortcut executes)
  │     Reset: usedForZoom = false, usedWithOtherKey = false
```

**Suppression technique:** Inject a dummy `VK_CONTROL` press/release via `SendInput` before the Win key-up reaches the shell. The shell interprets `Win+Ctrl` release, which has no Start Menu effect. (AC-2.1.16, AC-2.1.17)

**Critical edge case: Win+E, Win+D, Win+L, etc.** If the user presses Win, scrolls, then presses another key, `usedWithOtherKey` is set. On release, do NOT suppress — the user expects the Win+key shortcut to execute normally. Suppressing would break the shortcut. (AC-2.1.18)

**When modifier is not Win:** WinKeyManager is disabled entirely. It does nothing. Start Menu suppression is irrelevant for Ctrl/Alt/Shift modifiers. (AC-2.1.20, Phase 5)

## Hook Health Watchdog

A timer on the Main thread checks hook handle validity every 5 seconds. (R-05 mitigation)

- If `SetWindowsHookEx` returned a handle that is now invalid: re-install immediately, log a warning.
- If re-registration fails: display a tray notification (Phase 5) or set an error flag. Scroll-gesture zoom is temporarily unavailable.
- The watchdog itself must not do heavy work — it's on the Main thread's message pump.

## Common Mistakes

1. **Doing real work in the hook callback.** Even a single `std::string` construction or a debug `printf` can push the callback close to the system timeout under load. If you need debug output, set an atomic flag and log from outside the callback on a state transition.

2. **Consuming keyboard events beyond the four zoom keys.** The keyboard hook returns 1 *only* for `VK_OEM_PLUS` / `VK_ADD` / `VK_OEM_MINUS` / `VK_SUBTRACT`, and *only* while the configured modifier is held (see "Keyboard Hook" above). Consuming any other key — or consuming the zoom keys when the modifier is not held — would break the user's keyboard for all applications. WinKeyManager's suppression injects a *new* event; it does not consume the Win key event itself.

3. **Suppressing Start Menu when `usedWithOtherKey` is true.** If the user pressed Win+E (to open Explorer) after scrolling, suppression would prevent Explorer from opening. Check both flags.

4. **Calling `SendInput` from the mouse hook callback.** `SendInput` for Start Menu suppression is called only from the keyboard hook's Win key-up path (via WinKeyManager). Never call it from the mouse hook — it introduces re-entrancy risk and added latency.

5. **Reading `HIWORD(mouseData)` without sign extension for scroll delta.** `WM_MOUSEWHEEL`'s delta is a signed short packed in the high word. Use `GET_WHEEL_DELTA_WPARAM` or cast to `short` explicitly. Treating it as unsigned produces enormous positive values instead of negative scroll-down deltas.

6. **Forgetting `CallNextHookEx` on the pass-through path.** Every event that isn't consumed must be forwarded. Failing to call `CallNextHookEx` silently breaks the hook chain for other applications.

7. **Testing modifier state with `GetAsyncKeyState` instead of tracking key-down/key-up events.** `GetAsyncKeyState` is racy and can miss brief key presses. The hook already receives every key event — track state explicitly in the callback via the `KBDLLHOOKSTRUCT` fields.

8. **Doing the shortcut work inside the hook instead of posting a command.** Keyboard shortcuts (shipped in Phase 2 — do not regress) post a command (`ZOOM_IN`, `ZOOM_OUT`, `ZOOM_RESET`) to the lock-free queue; the actual zoom math and ZoomController's ANIMATING-mode transition happen on the Render thread when it drains the queue. The hook must only detect the chord, post the command, and (for the four zoom keys with the modifier held) consume both edges — never compute zoom state in the callback.

Plan: Multi-Monitor Support + FocusMonitor COM Fix        │
     │                                                           │
     │ Context                                                   │
     │                                                           │
     │ SmoothZoom Phase 6 has two remaining items: multi-monitor │
     │  support (AC-MM.01–04) and a FocusMonitor COM threading   │
     │ fix. Most Phase 6 hardening is already complete.          │
     │                                                           │
     │ Key API constraint: MagSetFullscreenTransform applies a   │
     │ global zoom across the entire virtual desktop. It cannot  │
     │ independently zoom one monitor while leaving others at    │
     │ 1.0x. This is the same behavior as Windows Magnifier in   │
     │ full-screen mode. AC-MM.01's "other monitors at 1.0x"     │
     │ requirement is not achievable with this API — deferred to │
     │  Desktop Duplication migration (R-01).                    │
     │                                                           │
     │ Good news: The current proportional mapping formula       │
     │ (offset = cursor * (1 - 1/zoom)) already works correctly  │
     │ on multi-monitor. For any cursor position on any monitor, │
     │  the formula naturally keeps the active physical          │
     │ monitor's display within its own virtual desktop content  │
     │ area. No formula change is needed for pointer tracking.   │
     │                                                           │
     │ What does need changing: Focus/caret element centering,   │
     │ deadzone scaling, and monitor transition detection.       │
     │                                                           │
     │ ---                                                       │
     │ Task 1: Multi-Monitor Support                             │
     │                                                           │
     │ 1A. RenderLoop — Per-frame monitor detection              │
     │                                                           │
     │ File: src/logic/RenderLoop.cpp                            │
     │                                                           │
     │ Add per-frame active monitor detection after              │
     │ GetCursorPos() (~line 342). Both MonitorFromPoint and     │
     │ GetMonitorInfo are lightweight shared-memory reads        │
     │ (~1us), no heap allocation, no mutex — safe for the hot   │
     │ path.                                                     │
     │                                                           │
     │ Add static state:                                         │
     │   static HMONITOR s_activeMonitor = nullptr;              │
     │                                                           │
     │ After GetCursorPos():                                     │
     │   POINT cursorPt = {rawPtrX, rawPtrY};                    │
     │   HMONITOR hMon = MonitorFromPoint(cursorPt,              │
     │ MONITOR_DEFAULTTONEAREST);                                │
     │   MONITORINFO monInfo = {};                               │
     │   monInfo.cbSize = sizeof(monInfo);                       │
     │   GetMonitorInfo(hMon, &monInfo);                         │
     │                                                           │
     │   int32_t monLeft   = monInfo.rcMonitor.left;             │
     │   int32_t monTop    = monInfo.rcMonitor.top;              │
     │   int32_t monWidth  = monInfo.rcMonitor.right -           │
     │ monInfo.rcMonitor.left;                                   │
     │   int32_t monHeight = monInfo.rcMonitor.bottom -          │
     │ monInfo.rcMonitor.top;                                    │
     │                                                           │
     │   // Log on monitor transition (state transition only,    │
     │ not per-frame)                                            │
     │   if (hMon != s_activeMonitor) {                          │
     │       s_activeMonitor = hMon;                             │
     │       SZ_LOG_INFO("RenderLoop", L"Monitor transition:     │
     │ rect=(%d,%d %dx%d)",                                      │
     │                   monLeft, monTop, monWidth, monHeight);  │
     │   }                                                       │
     │                                                           │
     │ 1B. RenderLoop — Per-monitor deadzone scaling (AC-MM.04)  │
     │                                                           │
     │ File: src/logic/RenderLoop.cpp ~line 347                  │
     │                                                           │
     │ Replace GetSystemMetrics(SM_CYSCREEN) (primary monitor    │
     │ height) with active monitor height from step 1A:          │
     │                                                           │
     │ // OLD:                                                   │
     │ int32_t primaryH = GetSystemMetrics(SM_CYSCREEN);         │
     │ int32_t deadzoneThreshold = primaryH > 0 ? (3 * primaryH  │
     │ / 1080) : 3;                                              │
     │                                                           │
     │ // NEW:                                                   │
     │ int32_t deadzoneThreshold = monHeight > 0 ? (3 *          │
     │ monHeight / 1080) : 3;                                    │
     │                                                           │
     │ 1C. RenderLoop — Per-monitor dimensions for focus/caret   │
     │ centering                                                 │
     │                                                           │
     │ File: src/logic/RenderLoop.cpp ~lines 407-425             │
     │                                                           │
     │ For pointer tracking, continue passing virtual desktop    │
     │ dimensions (current behavior — already correct for        │
     │ multi-monitor). For focus/caret tracking, pass            │
     │ per-monitor dimensions so the element centers on its      │
     │ physical monitor.                                         │
     │                                                           │
     │ When active source is Focus or Caret, determine which     │
     │ monitor the element is on:                                │
     │                                                           │
     │ case TrackingSource::Caret:                               │
     │     // Use caret's monitor for centering                  │
     │     {                                                     │
     │         ScreenPoint caretCenter = caretRect.center();     │
     │         POINT cp = {caretCenter.x, caretCenter.y};        │
     │         HMONITOR hElemMon = MonitorFromPoint(cp,          │
     │ MONITOR_DEFAULTTONEAREST);                                │
     │         MONITORINFO emi = {}; emi.cbSize = sizeof(emi);   │
     │         GetMonitorInfo(hElemMon, &emi);                   │
     │         int32_t eMonL = emi.rcMonitor.left, eMonT =       │
     │ emi.rcMonitor.top;                                        │
     │         int32_t eMonW = emi.rcMonitor.right - eMonL;      │
     │         int32_t eMonH = emi.rcMonitor.bottom - eMonT;     │
     │         targetOffset =                                    │
     │ ViewportTracker::computeCaretOffset(                      │
     │             caretRect, zoom, eMonW, eMonH, eMonL, eMonT); │
     │     }                                                     │
     │     break;                                                │
     │ case TrackingSource::Focus:                               │
     │     // Same pattern — use focus element's monitor         │
     │     ...                                                   │
     │ case TrackingSource::Pointer:                             │
     │     // Keep using virtual desktop dimensions (s_screenW,  │
     │ s_screenH, s_screenOriginX/Y)                             │
     │     targetOffset = ViewportTracker::computePointerOffset( │
     │         s_committedPtrX, s_committedPtrY, zoom,           │
     │ s_screenW, s_screenH,                                     │
     │         s_screenOriginX, s_screenOriginY);                │
     │                                                           │
     │ 1D. ViewportTracker — Updated centering for multi-monitor │
     │                                                           │
     │ File: src/logic/ViewportTracker.cpp                       │
     │                                                           │
     │ The current centering formula in computeElementOffset     │
     │ assumes the offset should center the element in the total │
     │  viewport. On multi-monitor, we need to center on the     │
     │ active monitor's physical display.                        │
     │                                                           │
     │ Centering formula change (both computeElementOffset and   │
     │ computeCaretOffset):                                      │
     │                                                           │
     │ // OLD: centers in total viewport (correct only when      │
     │ originX=0)                                                │
     │ float viewportW = static_cast<float>(screenW) / zoom;     │
     │ float xOff = static_cast<float>(center.x) - viewportW /   │
     │ 2.0f;                                                     │
     │                                                           │
     │ // NEW: centers on the physical monitor identified by     │
     │ origin/dimensions                                         │
     │ // Derivation: we want the element at the physical center │
     │  of the monitor.                                          │
     │ // Physical center of monitor = originX + screenW/2.      │
     │ // Zoom mapping: physX = (virtualX - offsetX) * zoom.     │
     │ // Setting physX = originX + screenW/2 and virtualX =     │
     │ center.x:                                                 │
     │ //   offsetX = center.x - (originX + screenW/2) / zoom    │
     │ float monCenterX = static_cast<float>(originX) +          │
     │ static_cast<float>(screenW) / 2.0f;                       │
     │ float xOff = static_cast<float>(center.x) - monCenterX /  │
     │ zoom;                                                     │
     │                                                           │
     │ Same for Y axis. When originX=0 (single monitor), this    │
     │ simplifies to the current formula — fully backward        │
     │ compatible.                                               │
     │                                                           │
     │ Clamping formula change (both computeElementOffset and    │
     │ computeCaretOffset):                                      │
     │                                                           │
     │ // OLD: assumes originX is the minimum valid offset       │
     │ float minOffX = static_cast<float>(originX);              │
     │ float maxOffX = static_cast<float>(originX) +             │
     │ static_cast<float>(screenW) * (1.0f - invZoom);           │
     │                                                           │
     │ // NEW: derived from keeping active monitor's display     │
     │ within its virtual area                                   │
     │ // Active monitor physical shows virtual columns [offX +  │
     │ originX/Z, offX + (originX+screenW)/Z)                    │
     │ // These must stay within [originX, originX+screenW]      │
     │ // => offX >= originX * (1 - 1/Z)  and  offX <=           │
     │ (originX+screenW) * (1 - 1/Z)                             │
     │ float minOffX = static_cast<float>(originX) * (1.0f -     │
     │ invZoom);                                                 │
     │ float maxOffX = static_cast<float>(originX + screenW) *   │
     │ (1.0f - invZoom);                                         │
     │                                                           │
     │ When originX=0: minOff=0, maxOff=screenW*(1-1/Z) — same   │
     │ as current. Backward compatible.                          │
     │                                                           │
     │ computePointerOffset — NO CHANGES. The pointer formula    │
     │ and clamping remain unchanged. RenderLoop continues       │
     │ passing virtual desktop dimensions for pointer tracking,  │
     │ which already works correctly on multi-monitor.           │
     │                                                           │
     │ 1E. Unit test updates                                     │
     │                                                           │
     │ File: tests/unit/test_ViewportTracker.cpp                 │
     │                                                           │
     │ - Existing single-monitor tests: unchanged (backward      │
     │ compatible)                                               │
     │ - Existing pointer multi-monitor tests (lines 379-468):   │
     │ unchanged (pointer still uses virtual desktop dims)       │
     │ - Update "Multi-monitor: element offset respects negative │
     │  origin" test (line 444) — this test passes virtual       │
     │ desktop dimensions to computeElementOffset, which now     │
     │ centers differently. Update expected bounds.              │
     │ - Add new tests:                                          │
     │   - Element centering on monitor 2 (originX=1920,         │
     │ screenW=1920): verify element at physical center of       │
     │ monitor                                                   │
     │   - Caret lookahead on secondary monitor                  │
     │   - Focus centering near monitor edges (clamp behavior)   │
     │                                                           │
     │ 1F. WM_DISPLAYCHANGE — no changes needed                  │
     │                                                           │
     │ main.cpp already handles WM_DISPLAYCHANGE (line 663) by   │
     │ updating SharedState atomics. RenderLoop reads these each │
     │  frame. MonitorFromPoint + GetMonitorInfo automatically   │
     │ reflect display changes. For monitor unplug (E6.9),       │
     │ MONITOR_DEFAULTTONEAREST gracefully falls back to the     │
     │ nearest remaining monitor.                                │
     │                                                           │
     │ API Limitation Note                                       │
     │                                                           │
     │ AC-MM.01 states "other monitors display at 1.0x." This is │
     │  not achievable with MagSetFullscreenTransform — the zoom │
     │  is global, so all monitors show zoomed content (same as  │
     │ Windows Magnifier). The implementation ensures the active │
     │  monitor's physical display shows content from its own    │
     │ virtual area (via correct offset computation), which is   │
     │ the best achievable UX with this API. Full per-monitor    │
     │ independent zoom requires Desktop Duplication API         │
     │ migration (R-01, out of scope).                           │
     │                                                           │
     │ ---                                                       │
     │ Task 2: FocusMonitor COM Threading Fix                    │
     │                                                           │
     │ The Bug                                                   │
     │                                                           │
     │ FocusMonitor.cpp line 92: std::async(std::launch::async,  │
     │ ...) + wait_for(100ms) appears to implement a timeout,    │
     │ but the timeout doesn't actually work. Per C++ standard,  │
     │ the destructor of a std::future from std::async blocks    │
     │ until the task completes. So when HandleFocusChangedEvent │
     │  returns after the timeout check, the future destructor   │
     │ still blocks until get_CurrentBoundingRectangle()         │
     │ finishes. A slow UIA provider can block the UIA thread    │
     │ indefinitely.                                             │
     │                                                           │
     │ The Fix: IUIAutomation6::ConnectionTimeout                │
     │                                                           │
     │ IUIAutomation6 (available since Windows 10 1809, our min  │
     │ target is 1903) exposes put_ConnectionTimeout(UINT ms)    │
     │ which bounds all cross-process UIA property queries at    │
     │ the COM infrastructure level.                             │
     │                                                           │
     │ File: src/input/FocusMonitor.cpp                          │
     │                                                           │
     │ Step 1: In threadMain() after CoCreateInstance (~line     │
     │ 144), query for IUIAutomation6 and set timeout:           │
     │                                                           │
     │ // Set 100ms timeout for UIA property queries (R-11       │
     │ mitigation)                                               │
     │ IUIAutomation6* automation6 = nullptr;                    │
     │ HRESULT hrTimeout = automation->QueryInterface(           │
     │     __uuidof(IUIAutomation6),                             │
     │ reinterpret_cast<void**>(&automation6));                  │
     │ if (SUCCEEDED(hrTimeout) && automation6) {                │
     │     automation6->put_ConnectionTimeout(100);  // 100ms    │
     │     automation6->Release();                               │
     │     SZ_LOG_INFO("FocusMonitor", L"UIA connection timeout  │
     │ set to 100ms");                                           │
     │ } else {                                                  │
     │     SZ_LOG_WARN("FocusMonitor", L"IUIAutomation6          │
     │ unavailable — no query timeout");                         │
     │ }                                                         │
     │                                                           │
     │ Step 2: In HandleFocusChangedEvent (~line 84), replace    │
     │ the std::async pattern with a direct call + timing:       │
     │                                                           │
     │ HRESULT STDMETHODCALLTYPE                                 │
     │ HandleFocusChangedEvent(IUIAutomationElement* sender)     │
     │ override                                                  │
     │ {                                                         │
     │     if (!sender || !state_) return S_OK;                  │
     │                                                           │
     │     // Direct call — bounded by                           │
     │ IUIAutomation6::ConnectionTimeout (R-11)                  │
     │     auto start = std::chrono::steady_clock::now();        │
     │     RECT boundingRect{};                                  │
     │     HRESULT hr =                                          │
     │ sender->get_CurrentBoundingRectangle(&boundingRect);      │
     │     auto elapsed = std::chrono::steady_clock::now() -     │
     │ start;                                                    │
     │     auto elapsedMs = std::chrono::duration_cast<std::chro │
     │ no::milliseconds>(elapsed).count();                       │
     │                                                           │
     │     // Log slow callbacks (R-11 mitigation item 3)        │
     │     if (elapsedMs > 50) {                                 │
     │         SZ_LOG_WARN("FocusMonitor", L"Slow UIA bounding   │
     │ rect query: %lldms", elapsedMs);                          │
     │     }                                                     │
     │                                                           │
     │     if (FAILED(hr)) return S_OK;  // Silent degradation   │
     │ (AC-2.5.14)                                               │
     │     if (!isValidRect(boundingRect)) return S_OK;  //      │
     │ Reject bad rects (R-09)                                   │
     │                                                           │
     │     ScreenRect rect;                                      │
     │     rect.left   = boundingRect.left;                      │
     │     rect.top    = boundingRect.top;                       │
     │     rect.right  = boundingRect.right;                     │
     │     rect.bottom = boundingRect.bottom;                    │
     │                                                           │
     │     state_->focusRect.write(rect);                        │
     │     state_->lastFocusChangeTime.store(currentTimeMs(),    │
     │ std::memory_order_release);                               │
     │     return S_OK;                                          │
     │ }                                                         │
     │                                                           │
     │ Step 3: Remove #include <future> (no longer needed).      │
     │                                                           │
     │ Fallback: If IUIAutomation6 is unavailable (shouldn't     │
     │ happen on Win10 1903+), the direct call runs without a    │
     │ timeout. The UIA thread is already isolated from main and │
     │  render threads, so a slow provider only delays focus     │
     │ updates — it cannot drop frames or stall input handling.  │
     │                                                           │
     │ ---                                                       │
     │ Files to Modify                                           │
     │                                                           │
     │ ┌──────────────────────────────┬───────────────────────── │
     │ ───┐                                                      │
     │ │             File             │          Changes         │
     │    │                                                      │
     │ ├──────────────────────────────┼───────────────────────── │
     │ ───┤                                                      │
     │ │                              │ Add                      │
     │ MonitorFromPoint/GetMo │                                  │
     │ │ src/logic/RenderLoop.cpp     │ nitorInfo, per-monitor   │
     │    │                                                      │
     │ │                              │ deadzone, per-monitor    │
     │    │                                                      │
     │ │                              │ focus/caret params       │
     │    │                                                      │
     │ ├──────────────────────────────┼───────────────────────── │
     │ ───┤                                                      │
     │ │                              │ Update centering +       │
     │    │                                                      │
     │ │ src/logic/ViewportTracker.cp │ clamping in              │
     │    │                                                      │
     │ │ p                            │ computeElementOffset and │
     │    │                                                      │
     │ │                              │ computeCaretOffset       │
     │    │                                                      │
     │ ├──────────────────────────────┼───────────────────────── │
     │ ───┤                                                      │
     │ │                              │ Replace std::async with  │
     │    │                                                      │
     │ │ src/input/FocusMonitor.cpp   │ IUIAutomation6 timeout + │
     │    │                                                      │
     │ │                              │ direct call              │
     │    │                                                      │
     │ ├──────────────────────────────┼───────────────────────── │
     │ ───┤                                                      │
     │ │                              │ Update multi-monitor     │
     │    │                                                      │
     │ │ tests/unit/test_ViewportTrac │ element test, add        │
     │    │                                                      │
     │ │ ker.cpp                      │ per-monitor centering    │
     │    │                                                      │
     │ │                              │ tests                    │
     │    │                                                      │
     │ └──────────────────────────────┴───────────────────────── │
     │ ───┘                                                      │
     │                                                           │
     │ Files NOT Modified                                        │
     │                                                           │
     │ - SharedState.h — no new fields needed (MonitorFromPoint  │
     │ queried directly per frame)                               │
     │ - MagBridge.cpp/h — setTransform passthrough unchanged    │
     │ - ViewportTracker.h — function signatures unchanged       │
     │ (origin/screenW semantics change is caller-side)          │
     │ - main.cpp — WM_DISPLAYCHANGE already handled             │
     │                                                           │
     │ ---                                                       │
     │ Verification                                              │
     │                                                           │
     │ 1. Build: cmake --build build --config Release — must     │
     │ compile cleanly                                           │
     │ 2. Unit tests: cd build && ctest -C Debug — all           │
     │ ViewportTracker tests pass                                │
     │ 3. Manual testing (multi-monitor):                        │
     │   - E6.4: Zoom on primary monitor, verify zoomed content  │
     │ tracks cursor                                             │
     │   - E6.5: Move cursor to second monitor while zoomed,     │
     │ verify smooth tracking continues                          │
     │   - E6.7: On different-resolution monitors, verify        │
     │ deadzone and viewport tracking adjust                     │
     │   - E6.9: Unplug monitor while zoomed — no crash          │
     │ 4. Manual testing (FocusMonitor):                         │
     │   - Tab between UI elements while zoomed — verify         │
     │ viewport follows focus                                    │
     │   - Test with a slow UIA app (Java app) — verify timeout  │
     │ logging appears, no thread hangs                          │
     │   - Verify SZ_LOG_WARN messages appear for slow (>50ms)   │
     │ queries in debug output   
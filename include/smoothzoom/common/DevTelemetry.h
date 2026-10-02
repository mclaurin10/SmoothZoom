#pragma once
// =============================================================================
// SmoothZoom — Dev-tools telemetry records (compiled only with SMOOTHZOOM_DEV_TOOLS)
//
// Plain-old-data records written on hot paths into PRE-ALLOCATED buffers:
//   WheelTapEvent   — main thread, from the WH_MOUSE_LL callback and the Raw
//                     Input / PTP accumulator writers (--record).
//   TransformSample — render thread, once per non-idle frameTick (--bench).
// No allocation, no locks, no I/O at the write sites (R-05; render-loop
// invariants 1-3). Buffers are owned and flushed by src/app/DevTools.cpp.
// =============================================================================

#ifdef SMOOTHZOOM_DEV_TOOLS

#include <atomic>
#include <cstdint>

namespace SmoothZoom
{

enum class WheelSource : uint8_t
{
    HookWheel  = 0, // WH_MOUSE_LL WM_MOUSEWHEEL
    HookHWheel = 1, // WH_MOUSE_LL WM_MOUSEHWHEEL
    RawInput   = 2, // main.cpp WM_INPUT RIM_TYPEMOUSE fallback (every event after dedup; consumed = zoomed)
    Ptp        = 3, // main.cpp precision-touchpad HID path (every two-finger scroll step; consumed = zoomed)
    Contact    = 4, // precision-touchpad finger count changed (delta = fingers down, 0 = lifted)
};

enum ModBits : uint8_t { kModShift = 1, kModCtrl = 2, kModAlt = 4, kModWin = 8 };

struct WheelTapEvent
{
    int64_t     qpc = 0;
    int32_t     x = 0, y = 0;       // MSLLHOOKSTRUCT::pt (hook) or GetCursorPos (RAW/PTP)
    uint32_t    flags = 0;          // MSLLHOOKSTRUCT::flags (LLMHF_INJECTED = 1, LLMHF_LOWER_IL_INJECTED = 2)
    int16_t     delta = 0;          // raw wheel delta (hook) / accumulator delta (RAW/PTP)
    WheelSource source = WheelSource::HookWheel;
    uint8_t     mods = 0;           // ModBits, physical state via GetAsyncKeyState
    bool        modifierHeld = false; // the hook's own modifier verdict
    bool        consumed = false;
};

using WheelTapFn = void (*)(const WheelTapEvent&);

// One record per frameTick that did not take the 1.0x idle short-circuit.
struct TransformSample
{
    int64_t  qpcTick = 0;        // frame start (the dt QPC read)
    int64_t  qpcSetBegin = 0;    // just before MagBridge::setTransform (0 if no update)
    int64_t  qpcSetEnd = 0;      // just after
    int64_t  qpcVBlank = 0;      // DWM_TIMING_INFO::qpcVBlank, read after the set
    uint64_t cRefresh = 0;       // DWM_TIMING_INFO::cRefresh
    int64_t  qpcRefreshPeriod = 0;
    float    targetZoom = 1.0f;  // ZoomController::targetZoom()
    float    zoom = 1.0f;        // zoom computed this frame (== sent when updated)
    float    idealOffX = 0, idealOffY = 0;   // float proportional offset from the RAW cursor
    float    offX = 0, offY = 0;             // float offset RenderLoop computed (any source)
    int32_t  appliedX = 0, appliedY = 0;     // offsets the API holds (MagGetFullscreenTransform read-back)
    float    rbZoom = 1.0f;                  // magnification the API holds (read-back)
    int32_t  cursorX = 0, cursorY = 0;       // raw GetCursorPos
    int32_t  scrollDelta = 0;    // accumulator delta consumed this frame
    uint8_t  source = 0;         // TrackingSource
    uint8_t  mode = 0;           // ZoomController::Mode
    uint8_t  updated = 0;        // setTransform was called this frame
    uint8_t  ok = 1;             // setTransform result
};

struct TransformTelemetry
{
    TransformSample*      samples = nullptr;   // capacity entries, allocated before the render thread starts
    uint32_t              capacity = 0;
    std::atomic<uint32_t> count{0};            // published with release after each write
    std::atomic<uint32_t> dropped{0};
    std::atomic<bool>     enabled{false};
};

} // namespace SmoothZoom

#endif // SMOOTHZOOM_DEV_TOOLS

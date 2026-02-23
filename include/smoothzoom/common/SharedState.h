#pragma once
// =============================================================================
// SmoothZoom — Shared State
// All inter-thread shared data in one place. Doc 3 §2.4.
// Written by hook callbacks (main thread) and UIA thread.
// Read by render thread — no mutexes on hot path.
// =============================================================================

#include "smoothzoom/common/Types.h"
#include "smoothzoom/common/SeqLock.h"
#include "smoothzoom/common/LockFreeQueue.h"
#include <atomic>

namespace SmoothZoom
{

struct SharedState
{
    // -- Written by main thread (hook callbacks) --
    std::atomic<bool>    modifierHeld{false};
    std::atomic<int32_t> pointerX{0};
    std::atomic<int32_t> pointerY{0};
    std::atomic<int32_t> scrollAccumulator{0};
    std::atomic<bool>    toggleState{false};
    std::atomic<int64_t> lastKeyboardInputTime{0};

    // -- Written by UIA thread --
    SeqLock<ScreenRect>  focusRect;
    SeqLock<ScreenRect>  caretRect;
    std::atomic<int64_t> lastFocusChangeTime{0};

    // -- Command queue: main thread → render thread --
    LockFreeQueue<ZoomCommand> commandQueue;
};

} // namespace SmoothZoom

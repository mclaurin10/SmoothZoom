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
#include "smoothzoom/support/SettingsManager.h"
#include <atomic>
#include <memory>

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

    // -- Written by render thread, read by main thread --
    std::atomic<float> currentZoomLevel{1.0f};

    // -- Command queue: main thread → render thread --
    LockFreeQueue<ZoomCommand> commandQueue;

    // -- Settings snapshot: written by main thread, read by all --
    // Render thread checks settingsVersion (one atomic int) per frame.
    // Only does the heavier shared_ptr atomic_load when version changes.
    std::shared_ptr<const SettingsSnapshot> settingsSnapshot;
    std::atomic<uint64_t> settingsVersion{0};
};

} // namespace SmoothZoom

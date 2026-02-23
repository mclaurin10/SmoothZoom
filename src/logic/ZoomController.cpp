// =============================================================================
// SmoothZoom — ZoomController
// Zoom level state, scroll accumulation, animation targets. Doc 3 §3.3
//
// Phase 0: Simple linear zoom from scroll delta. No logarithmic model yet.
// Phase 1: Logarithmic scroll model (AC-2.1.06), soft bounds (AC-2.1.15).
// Phase 2: ANIMATING mode with ease-out (AC-2.2.04–AC-2.2.07).
// Phase 4: TOGGLING mode (AC-2.7.01–AC-2.7.12).
// =============================================================================

#include "smoothzoom/logic/ZoomController.h"
#include <algorithm>
#include <cmath>

namespace SmoothZoom
{

// Phase 0: simple linear zoom — each WHEEL_DELTA (120 units) changes zoom
// by a fixed fraction of the current level. This is intentionally simple;
// the logarithmic model (AC-2.1.06) comes in Phase 1.
static constexpr float kWheelDelta = 120.0f;
static constexpr float kScrollZoomFactor = 0.1f; // 10% per notch

// Epsilon for snapping to 1.0× (R-17)
static constexpr float kSnapEpsilon = 0.005f;

void ZoomController::applyScrollDelta(int32_t accumulatedDelta)
{
    if (accumulatedDelta == 0)
        return;

    mode_ = Mode::Scrolling;

    // Phase 0: linear multiply. Each notch = kScrollZoomFactor of current zoom.
    float normalizedDelta = static_cast<float>(accumulatedDelta) / kWheelDelta;
    float change = currentZoom_ * kScrollZoomFactor * normalizedDelta;
    float newZoom = currentZoom_ + change;

    // Clamp to bounds
    newZoom = std::clamp(newZoom, minZoom_, maxZoom_);

    // Snap to 1.0× within epsilon (R-17)
    if (std::abs(newZoom - 1.0f) < kSnapEpsilon)
        newZoom = 1.0f;

    // Snap to max within epsilon
    if (std::abs(newZoom - maxZoom_) < kSnapEpsilon)
        newZoom = maxZoom_;

    currentZoom_ = newZoom;
    targetZoom_ = newZoom;
}

void ZoomController::applyKeyboardStep(int direction)
{
    // Phase 2 — stub for now
    float newTarget = targetZoom_ + (static_cast<float>(direction) * keyboardStep_);
    newTarget = std::clamp(newTarget, minZoom_, maxZoom_);
    targetZoom_ = newTarget;
    mode_ = Mode::Animating;
}

bool ZoomController::tick(float /*dtSeconds*/)
{
    // Phase 0: no animation — scroll input is applied directly.
    // Phase 2 adds ease-out interpolation for ANIMATING mode.

    if (mode_ == Mode::Idle)
        return false;

    if (mode_ == Mode::Scrolling)
    {
        // Scroll-direct: no interpolation needed. Value was set in applyScrollDelta.
        // Transition to Idle when scroll input stops (done externally by RenderLoop
        // when scrollAccumulator is 0 for a frame and modifier is released).
        return true;
    }

    // Phase 2+: ANIMATING / TOGGLING interpolation would go here
    return false;
}

void ZoomController::reset()
{
    currentZoom_ = 1.0f;
    targetZoom_ = 1.0f;
    mode_ = Mode::Idle;
}

} // namespace SmoothZoom

// =============================================================================
// SmoothZoom — ZoomController
// Zoom level state, scroll accumulation, animation targets. Doc 3 §3.5
//
// Phase 1: Logarithmic scroll model (AC-2.1.06), soft bounds (AC-2.1.15).
// Phase 2: ANIMATING mode with ease-out (AC-2.2.04–AC-2.2.07).
// Phase 4: TOGGLING mode (AC-2.7.01–AC-2.7.12).
// =============================================================================

#include "smoothzoom/logic/ZoomController.h"
#include <algorithm>
#include <cmath>

namespace SmoothZoom
{

static constexpr float kWheelDelta = 120.0f;

// Logarithmic zoom factor per notch (AC-2.1.06): newZoom = currentZoom * pow(kScrollZoomBase, normalizedDelta)
// 1.1 = 10% per notch at any zoom level. 1×→2× requires same scroll effort as 5×→10×.
static constexpr float kScrollZoomBase = 1.1f;

// Epsilon for snapping to 1.0× and maxZoom (R-17)
static constexpr float kSnapEpsilon = 0.005f;

// Soft-approach margin as fraction of log-range near bounds (AC-2.1.15)
static constexpr float kSoftMarginFraction = 0.15f;

void ZoomController::applyScrollDelta(int32_t accumulatedDelta)
{
    if (accumulatedDelta == 0)
        return;

    mode_ = Mode::Scrolling;

    // Logarithmic zoom model (AC-2.1.06):
    // Each 120-unit notch multiplies zoom by kScrollZoomBase.
    // Sub-notch deltas (Precision Touchpad) scale proportionally.
    float normalizedDelta = static_cast<float>(accumulatedDelta) / kWheelDelta;

    // Soft-approach bounds attenuation (AC-2.1.15):
    // As zoom nears min or max, attenuate the delta to decelerate smoothly.
    float logMin = std::log(minZoom_);
    float logMax = std::log(maxZoom_);
    float logRange = logMax - logMin;
    float margin = logRange * kSoftMarginFraction;
    float logCurrent = std::log(currentZoom_);

    if (normalizedDelta > 0.0f && logCurrent > logMax - margin)
    {
        // Approaching upper bound — attenuate
        float t = (logCurrent - (logMax - margin)) / margin;
        t = std::clamp(t, 0.0f, 1.0f);
        float attenuation = 1.0f - t * t; // quadratic ease to zero
        normalizedDelta *= attenuation;
    }
    else if (normalizedDelta < 0.0f && logCurrent < logMin + margin)
    {
        // Approaching lower bound — attenuate
        float t = ((logMin + margin) - logCurrent) / margin;
        t = std::clamp(t, 0.0f, 1.0f);
        float attenuation = 1.0f - t * t;
        normalizedDelta *= attenuation;
    }

    float newZoom = currentZoom_ * std::pow(kScrollZoomBase, normalizedDelta);

    // Hard clamp to bounds (safety net after soft approach)
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

// =============================================================================
// SmoothZoom — ZoomController
// Zoom level state, scroll accumulation, animation targets. Doc 3 §3.5
//
// Phase 1: Logarithmic scroll model (AC-2.1.06), soft bounds (AC-2.1.15).
// Phase 2: ANIMATING mode with ease-out (AC-2.2.04–AC-2.2.07).
// Phase 4: Temporary toggle via engageToggle/releaseToggle (AC-2.7.01–AC-2.7.10).
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

// Exponential ease-out rate: ~0.15 per frame at 60fps (render-loop.md, AC-2.2.05)
// Used as: alpha = 1.0 - pow(1.0 - kEaseOutRate, dt * kReferenceHz)
static constexpr double kEaseOutRate = 0.15;
static constexpr double kReferenceHz = 60.0;

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

    // Phase 4: Update toggle restore target if scrolling during toggle (AC-2.7.09 / E4.5)
    if (isToggled_)
        savedZoomForToggle_ = currentZoom_;

    // Track last-used zoom for "toggle from 1.0×" (AC-2.7.04)
    if (currentZoom_ > 1.0f + kSnapEpsilon)
        lastUsedZoom_ = currentZoom_;
}

void ZoomController::applyKeyboardStep(int direction)
{
    // Phase 2: Multiplicative step for logarithmic consistency (AC-2.1.06, AC-2.2.06, AC-2.8.07)
    // direction=+1 → target *= (1 + step), direction=-1 → target /= (1 + step)
    float newTarget = targetZoom_ * std::pow(1.0f + keyboardStep_, static_cast<float>(direction));
    newTarget = std::clamp(newTarget, minZoom_, maxZoom_);

    // Snap within epsilon of 1.0 and max (R-17)
    if (std::abs(newTarget - 1.0f) < kSnapEpsilon)
        newTarget = 1.0f;
    if (std::abs(newTarget - maxZoom_) < kSnapEpsilon)
        newTarget = maxZoom_;

    // No-effect check at bounds (AC-2.8.05): if step produces no change, don't animate
    if (std::abs(newTarget - targetZoom_) < kSnapEpsilon)
        return;

    targetZoom_ = newTarget;
    mode_ = Mode::Animating;

    // Phase 4: Update toggle restore target if keyboard step during toggle (AC-2.7.09)
    if (isToggled_)
        savedZoomForToggle_ = targetZoom_;

    // Track last-used zoom for "toggle from 1.0×" (AC-2.7.04)
    if (targetZoom_ > 1.0f + kSnapEpsilon)
        lastUsedZoom_ = targetZoom_;
}

void ZoomController::animateToZoom(float target)
{
    target = std::clamp(target, minZoom_, maxZoom_);

    // Snap within epsilon (R-17)
    if (std::abs(target - 1.0f) < kSnapEpsilon)
        target = 1.0f;
    if (std::abs(target - maxZoom_) < kSnapEpsilon)
        target = maxZoom_;

    // No-effect: already at target (AC-2.8.09)
    if (std::abs(currentZoom_ - target) < kSnapEpsilon &&
        std::abs(targetZoom_ - target) < kSnapEpsilon)
    {
        return;
    }

    targetZoom_ = target;
    mode_ = Mode::Animating;
}

void ZoomController::engageToggle()
{
    if (isToggled_)
        return; // Idempotent (AC-2.7.07 edge case)

    savedZoomForToggle_ = currentZoom_;
    isToggled_ = true;

    if (std::abs(currentZoom_ - 1.0f) < kSnapEpsilon)
    {
        // At 1.0×: toggle to last-used zoom (AC-2.7.04), default 2.0× (AC-2.7.05)
        animateToZoom(lastUsedZoom_);
    }
    else
    {
        // Zoomed in: save as last-used, toggle to 1.0× (AC-2.7.01)
        lastUsedZoom_ = currentZoom_;
        animateToZoom(1.0f);
    }
}

void ZoomController::releaseToggle()
{
    if (!isToggled_)
        return; // Idempotent

    isToggled_ = false;
    animateToZoom(savedZoomForToggle_); // AC-2.7.03
}

bool ZoomController::tick(float dtSeconds)
{
    if (mode_ == Mode::Idle)
        return false;

    if (mode_ == Mode::Scrolling)
    {
        // Scroll-direct: no interpolation needed. Value was set in applyScrollDelta.
        // Transition to Idle when scroll input stops (done externally by RenderLoop
        // when scrollAccumulator is 0 for a frame and modifier is released).
        return true;
    }

    if (mode_ == Mode::Animating)
    {
        // Exponential ease-out (render-loop.md, AC-2.2.05):
        // Frame-rate-independent: at 60fps alpha≈0.15, at 144fps alpha≈0.065.
        double dt = static_cast<double>(dtSeconds);
        if (dt <= 0.0)
            dt = 1.0 / kReferenceHz; // Fallback if dt is zero or negative
        if (dt > 0.1)
            dt = 0.1; // Clamp to avoid huge jumps (debugger break, system sleep)

        double alpha = 1.0 - std::pow(1.0 - kEaseOutRate, dt * kReferenceHz);

        double current = static_cast<double>(currentZoom_);
        double target = static_cast<double>(targetZoom_);
        double newZoom = current + (target - current) * alpha;

        // Snap to target within epsilon — prevents infinite asymptotic approach
        if (std::abs(newZoom - target) < static_cast<double>(kSnapEpsilon))
        {
            currentZoom_ = targetZoom_;
            mode_ = Mode::Idle;
            return true;
        }

        currentZoom_ = static_cast<float>(newZoom);
        return true;
    }

    return false;
}

void ZoomController::applySettings(float minZoom, float maxZoom,
                                    float keyboardStep, float defaultZoomLevel)
{
    minZoom_ = minZoom;
    maxZoom_ = maxZoom;
    keyboardStep_ = keyboardStep;
    lastUsedZoom_ = defaultZoomLevel; // Update default for toggle-from-1.0× (AC-2.7.05)

    // AC-2.9.05: zoomed above new max → animate down
    if (currentZoom_ > maxZoom_)
        animateToZoom(maxZoom_);

    // AC-2.9.06: zoomed below new min → animate up
    if (currentZoom_ < minZoom_)
        animateToZoom(minZoom_);

    // Clamp pending target to new bounds
    targetZoom_ = std::clamp(targetZoom_, minZoom_, maxZoom_);
}

void ZoomController::reset()
{
    currentZoom_ = 1.0f;
    targetZoom_ = 1.0f;
    mode_ = Mode::Idle;
}

} // namespace SmoothZoom

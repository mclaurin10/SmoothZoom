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

// Exponential ease-out reference rate (render-loop.md, AC-2.2.05)
// Used as: alpha = 1.0 - pow(1.0 - easeOutRate_, dt * kReferenceHz)
// Speed variants: slow=0.08, normal=0.15, fast=0.25
static constexpr double kEaseOutRateSlow   = 0.08;
static constexpr double kEaseOutRateNormal = 0.15;
static constexpr double kEaseOutRateFast   = 0.25;
static constexpr double kReferenceHz = 60.0;

// Soft-approach margin as fraction of log-range near bounds (AC-2.1.15)
static constexpr float kSoftMarginFraction = 0.15f;

// Whole-notch scroll smoothing (AC-2.2.03, Doc 3 §3.5). The displayed log-zoom
// follows log(target) as a critically damped system: smooth onset, no
// overshoot, retargets on every notch. kSmoothOmega sets the response (an
// isolated notch is ~50% applied on its first frame at 60 Hz). The tail ends
// on the last frame that is still within kSmoothRestBy of the newest
// target-moving notch (timed from the input, not the frame), so the zoom
// rests within the bench's 100 ms settle bound (C-14) yet keeps moving
// across notch gaps up to that long (no 2-refresh hole, C-08). It also ends
// once within kSmoothSnapLog (0.001%, invisible).
static constexpr double kSmoothOmega = 100.0;       // 1/s
static constexpr double kSmoothRestBy = 0.097;      // s after the newest notch input
static constexpr double kSmoothSnapLog = 1e-5;
// After any sub-notch delta, the input is treated as continuous (touchpad)
// for this long: a touchpad frame that happens to sum to 120 stays direct.
static constexpr double kFineHoldSeconds = 0.25;

void ZoomController::applyScrollDelta(int32_t accumulatedDelta, double inputAgeSeconds,
                                      ScrollDevice device)
{
    if (accumulatedDelta == 0)
        return;

    // Scroll takes over from whatever is on screen (AC-2.2.08): a keyboard
    // animation or toggle stops, and the new target builds on the displayed level.
    if (mode_ != Mode::Scrolling)
    {
        targetZoom_ = currentZoom_;
        stopSmoothing();
    }
    mode_ = Mode::Scrolling;

    const bool notched = (accumulatedDelta % static_cast<int32_t>(kWheelDelta) == 0) && fineHold_ <= 0.0;
    if (!notched)
        fineHold_ = kFineHoldSeconds;

    // Logarithmic zoom model (AC-2.1.06):
    // Each 120-unit notch multiplies zoom by kScrollZoomBase.
    // Sub-notch deltas (Precision Touchpad) scale proportionally.
    // The device's sensitivity scales the effective input so users can tune
    // the zoom rate per device (A3, AC-2.9.20); 1.0 = default, leaving prior
    // behaviour unchanged.
    const float sensitivity =
        (device == ScrollDevice::Touchpad) ? touchpadSensitivity_ : scrollSensitivity_;
    float normalizedDelta =
        static_cast<float>(accumulatedDelta) / kWheelDelta * sensitivity;

    // Soft-approach bounds attenuation (AC-2.1.15):
    // As zoom nears min or max, attenuate the delta to decelerate smoothly.
    float logMin = std::log(minZoom_);
    float logMax = std::log(maxZoom_);
    float logRange = logMax - logMin;
    float margin = logRange * kSoftMarginFraction;
    float logCurrent = std::log(targetZoom_);   // the accumulated level, not the smoothed display

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

    float newZoom = targetZoom_ * std::pow(kScrollZoomBase, normalizedDelta);

    // Hard clamp to bounds (safety net after soft approach)
    newZoom = std::clamp(newZoom, minZoom_, maxZoom_);

    // Snap to 1.0× within epsilon (R-17)
    if (std::abs(newZoom - 1.0f) < kSnapEpsilon)
        newZoom = 1.0f;

    // Snap to max within epsilon
    if (std::abs(newZoom - maxZoom_) < kSnapEpsilon)
        newZoom = maxZoom_;

    const bool moved = (newZoom != targetZoom_);
    targetZoom_ = newZoom;
    if (notched)
    {
        // A notch pushing into a bound leaves the target where it is; it must
        // not keep a finished motion alive (the rest time is from the last
        // notch that moved the target).
        if (moved)
        {
            if (!smoothing_)
            {
                smoothLog_ = std::log(static_cast<double>(currentZoom_));
                smoothVel_ = 0.0;
                smoothing_ = true;
            }
            notchAge_ = (inputAgeSeconds > 0.0) ? inputAgeSeconds : 0.0;
            notchFresh_ = true;
        }
    }
    else
    {
        currentZoom_ = newZoom;   // continuous input stays direct
        stopSmoothing();
    }

    // Phase 4: Update toggle restore target if scrolling during toggle (AC-2.7.09 / E4.5)
    if (isToggled_)
        savedZoomForToggle_ = targetZoom_;

    // Track last-used zoom for "toggle from 1.0×" (AC-2.7.04)
    if (targetZoom_ > 1.0f + kSnapEpsilon)
        lastUsedZoom_ = targetZoom_;
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
    stopSmoothing();

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
    stopSmoothing();
}

void ZoomController::trayToggle()
{
    const float level = restingZoom();
    if (std::abs(level - 1.0f) < kSnapEpsilon)
    {
        // At 1.0×: animate to last-used/default zoom
        animateToZoom(lastUsedZoom_);
    }
    else
    {
        // Zoomed: save as last-used, animate to 1.0×
        lastUsedZoom_ = level;
        animateToZoom(1.0f);
    }
    // Does NOT set isToggled_ — permanent switch, not hold-to-peek
}

void ZoomController::engageToggle()
{
    if (isToggled_)
        return; // Idempotent (AC-2.7.07 edge case)

    // Mid-way through notch smoothing the user's level is the scroll target.
    const float level = restingZoom();
    savedZoomForToggle_ = level;
    isToggled_ = true;

    if (std::abs(level - 1.0f) < kSnapEpsilon)
    {
        // At 1.0×: toggle to last-used zoom (AC-2.7.04), default 2.0× (AC-2.7.05)
        animateToZoom(lastUsedZoom_);
    }
    else
    {
        // Zoomed in: save as last-used, toggle to 1.0× (AC-2.7.01)
        lastUsedZoom_ = level;
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
    if (fineHold_ > 0.0)
        fineHold_ -= (dtSeconds > 0.0f) ? static_cast<double>(dtSeconds) : 1.0 / kReferenceHz;

    if (mode_ == Mode::Idle)
        return false;

    if (mode_ == Mode::Scrolling)
    {
        // Continuous input was applied directly in applyScrollDelta(). RenderLoop
        // calls endScroll() on a frame with no scroll input to return us to Idle
        // (which arms the 1.0× idle short-circuit). Until then we report
        // "changed"; RenderLoop gates its MagBridge call on its own
        // changed-since-last-frame check.
        if (!smoothing_)
            return true;

        // Whole-notch smoothing: exact critically damped step in log space,
        // y = displayed - goal, so a variable frame time stays stable.
        double dt = static_cast<double>(dtSeconds);
        if (dt <= 0.0)
            dt = 1.0 / kReferenceHz;
        if (dt > 0.1)
            dt = 0.1;
        if (notchFresh_)
            notchFresh_ = false;   // age was measured at this frame
        else
            notchAge_ += dt;

        const double goal = std::log(static_cast<double>(targetZoom_));
        const double y0 = smoothLog_ - goal;
        const double v0 = smoothVel_;
        const double decay = std::exp(-kSmoothOmega * dt);
        const double c = v0 + kSmoothOmega * y0;
        const double y = (y0 + c * dt) * decay;
        const double v = (v0 - kSmoothOmega * c * dt) * decay;

        // Rest now if the next frame (about dt away) would land past kSmoothRestBy.
        if (notchAge_ + dt > kSmoothRestBy
            || (std::abs(y) < kSmoothSnapLog && std::abs(v) / kReferenceHz < kSmoothSnapLog))
        {
            currentZoom_ = targetZoom_;   // at rest (exact, so 1.0x snaps to 1.0f)
            stopSmoothing();
        }
        else
        {
            smoothLog_ = goal + y;
            smoothVel_ = v;
            currentZoom_ = static_cast<float>(std::exp(smoothLog_));
        }
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

        double alpha = 1.0 - std::pow(1.0 - easeOutRate_, dt * kReferenceHz);

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
                                    float keyboardStep, float defaultZoomLevel,
                                    int animationSpeed, float scrollSensitivity,
                                    float touchpadSensitivity)
{
    minZoom_ = minZoom;
    maxZoom_ = maxZoom;
    keyboardStep_ = keyboardStep;
    lastUsedZoom_ = defaultZoomLevel; // Update default for toggle-from-1.0× (AC-2.7.05)

    // Scroll sensitivity (A3): guard against non-positive values from a crafted
    // config; the SettingsManager validates range, this is defense-in-depth.
    scrollSensitivity_ = (scrollSensitivity > 0.0f) ? scrollSensitivity : 1.0f;
    touchpadSensitivity_ = (touchpadSensitivity > 0.0f) ? touchpadSensitivity : 1.0f;

    // Wire animation speed: 0=slow, 1=normal, 2=fast
    switch (animationSpeed)
    {
    case 0:  easeOutRate_ = kEaseOutRateSlow;   break;
    case 2:  easeOutRate_ = kEaseOutRateFast;   break;
    default: easeOutRate_ = kEaseOutRateNormal; break;
    }

    // AC-2.9.05: zoomed above new max → animate down
    if (restingZoom() > maxZoom_)
        animateToZoom(maxZoom_);

    // AC-2.9.06: zoomed below new min → animate up
    if (restingZoom() < minZoom_)
        animateToZoom(minZoom_);

    // Clamp pending target to new bounds
    targetZoom_ = std::clamp(targetZoom_, minZoom_, maxZoom_);
}

void ZoomController::endScroll()
{
    // Only ends a scroll gesture. Never interrupts an in-progress animation
    // (Animating) or disturbs an already-Idle controller, and waits for notch
    // smoothing to come to rest (tick() keeps advancing it meanwhile).
    if (mode_ == Mode::Scrolling && !smoothing_)
        mode_ = Mode::Idle;
}

void ZoomController::reset()
{
    currentZoom_ = 1.0f;
    targetZoom_ = 1.0f;
    mode_ = Mode::Idle;
    stopSmoothing();
}

} // namespace SmoothZoom

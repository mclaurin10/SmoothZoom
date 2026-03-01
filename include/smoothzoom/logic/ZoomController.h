#pragma once
// =============================================================================
// SmoothZoom — ZoomController
// Zoom level state, scroll accumulation, animation targets. Doc 3 §3.5
// =============================================================================

#include <cstdint>

namespace SmoothZoom
{

class ZoomController
{
public:
    enum class Mode
    {
        Idle,       // At rest, no animation
        Scrolling,  // Actively receiving scroll input
        Animating,  // Ease-out in progress (keyboard step, toggle, or animateToZoom)
    };

    // Scroll-gesture zoom: consume accumulated delta, compute new level
    void applyScrollDelta(int32_t accumulatedDelta);

    // Keyboard step: set animation target (+1 = zoom in, -1 = zoom out)
    void applyKeyboardStep(int direction);

    // Animation tick: advance currentZoom toward targetZoom (ease-out)
    // Returns true if zoom value changed this frame.
    bool tick(float dtSeconds);

    // Animate to an arbitrary target zoom (Win+Esc → 1.0×, Phase 4 toggle, etc.)
    void animateToZoom(float target);

    // Phase 4: Temporary toggle (hold-to-peek) — AC-2.7.01 through AC-2.7.10
    void engageToggle();
    void releaseToggle();
    bool isToggled() const { return isToggled_; }

    // Phase 5C: One-shot tray toggle (AC-2.9.15) — permanent switch, not hold-to-peek
    void trayToggle();

    // Phase 5: Apply settings from snapshot. Called by render thread when it
    // detects a new settings version. Triggers animation if zoom is out of new
    // bounds (AC-2.9.05, AC-2.9.06).
    // animationSpeed: 0=slow, 1=normal, 2=fast
    void applySettings(float minZoom, float maxZoom, float keyboardStep,
                       float defaultZoomLevel, int animationSpeed);

    // Reset to 1.0× instantly (shutdown path)
    void reset();

    // Accessors
    float currentZoom() const { return currentZoom_; }
    float targetZoom() const { return targetZoom_; }
    Mode mode() const { return mode_; }

private:
    float currentZoom_ = 1.0f;
    float targetZoom_ = 1.0f;
    float minZoom_ = 1.0f;
    float maxZoom_ = 10.0f;
    float keyboardStep_ = 0.25f;
    Mode mode_ = Mode::Idle;

    // Animation ease-out rate (configurable via animationSpeed setting)
    double easeOutRate_ = 0.15;  // Default: normal speed

    // Phase 4: Toggle state
    bool isToggled_ = false;
    float savedZoomForToggle_ = 1.0f;
    float lastUsedZoom_ = 2.0f;  // Default per AC-2.7.05
};

} // namespace SmoothZoom

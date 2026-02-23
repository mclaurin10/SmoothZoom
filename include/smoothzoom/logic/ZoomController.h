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
        Animating,  // Keyboard-initiated ease-out in progress
        Toggling,   // Temporary toggle animation in progress
    };

    // Scroll-gesture zoom: consume accumulated delta, compute new level
    void applyScrollDelta(int32_t accumulatedDelta);

    // Keyboard step: set animation target (+1 = zoom in, -1 = zoom out)
    void applyKeyboardStep(int direction);

    // Animation tick: advance currentZoom toward targetZoom (ease-out)
    // Returns true if zoom value changed this frame.
    bool tick(float dtSeconds);

    // Reset to 1.0×
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
};

} // namespace SmoothZoom

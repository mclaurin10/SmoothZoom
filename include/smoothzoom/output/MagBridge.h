#pragma once
// =============================================================================
// SmoothZoom — MagBridge
// Sole abstraction over the Magnification API. Doc 3 §3.8
// No other component calls Magnification API functions directly.
// Future migration to Desktop Duplication API is bounded to this component.
// =============================================================================

namespace SmoothZoom
{

class MagBridge
{
public:
    bool initialize();
    void shutdown();

    // Per-frame: apply zoom level and viewport offset
    bool setTransform(float magnification, float xOffset, float yOffset);

    // Per-frame: keep input coordinates accurate while zoomed
    bool setInputTransform(float magnification, float xOffset, float yOffset);

    // Query current state (startup conflict detection — AC-ERR.01)
    bool getTransform(float& magnification, float& xOffset, float& yOffset) const;

    // Color inversion (Phase 6)
    bool setColorInversion(bool enabled);

private:
    bool initialized_ = false;
};

} // namespace SmoothZoom

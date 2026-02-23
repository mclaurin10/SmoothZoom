#pragma once
// =============================================================================
// SmoothZoom — SettingsManager
// Loads, validates, saves config.json. Thread-safe snapshot model. Doc 3 §3.9
// Phase 5 component (stubbed until then).
// =============================================================================

#include <atomic>
#include <memory>

namespace SmoothZoom
{

struct SettingsSnapshot
{
    int     modifierKeyVK       = 0x5B;  // VK_LWIN
    float   minZoom             = 1.0f;
    float   maxZoom             = 10.0f;
    float   keyboardZoomStep    = 0.25f;
    int     animationSpeed      = 1;     // 0=slow, 1=normal, 2=fast
    bool    imageSmoothingEnabled = true;
    bool    startWithWindows    = false;
    bool    startZoomed         = false;
    float   defaultZoomLevel    = 2.0f;
    bool    followKeyboardFocus = true;
    bool    followTextCursor    = true;
    bool    colorInversionEnabled = false;
};

class SettingsManager
{
public:
    bool loadFromFile(const char* path);
    bool saveToFile(const char* path) const;

    // Thread-safe: returns an immutable snapshot (atomic pointer swap)
    std::shared_ptr<const SettingsSnapshot> snapshot() const;

private:
    std::atomic<std::shared_ptr<const SettingsSnapshot>> current_;
};

} // namespace SmoothZoom

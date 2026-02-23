// =============================================================================
// SmoothZoom — SettingsManager (STUB)
// Config persistence and thread-safe snapshots. Doc 3 §3.9
// Phase 5 component — not implemented until then.
// =============================================================================

#include "smoothzoom/support/SettingsManager.h"

namespace SmoothZoom
{

bool SettingsManager::loadFromFile(const char* /*path*/)
{
    // Phase 5: JSON parsing from %AppData%\SmoothZoom\config.json
    return false;
}

bool SettingsManager::saveToFile(const char* /*path*/) const
{
    // Phase 5: JSON serialization
    return false;
}

std::shared_ptr<const SettingsSnapshot> SettingsManager::snapshot() const
{
    // Thread-safe read via C++17 atomic shared_ptr operations
    return std::atomic_load(&current_);
}

} // namespace SmoothZoom

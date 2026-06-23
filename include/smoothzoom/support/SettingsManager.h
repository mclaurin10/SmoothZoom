#pragma once
// =============================================================================
// SmoothZoom — SettingsManager
// Loads, validates, saves config.json. Thread-safe snapshot model. Doc 3 §3.9
//
// Phase 5A: JSON persistence, validation, atomic snapshot distribution.
// =============================================================================

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace SmoothZoom
{

// Config schema version. Bump when a field's meaning changes or a migration is
// needed. saveToFile() always stamps the CURRENT version; loadFromFile() records
// the version the file was written with (0 = legacy / pre-versioning) so future
// code can detect and migrate older configs. (Forward-compatibility, no silent
// default of changed fields.)
inline constexpr int kSettingsSchemaVersion = 1;

struct SettingsSnapshot
{
    // Schema version this snapshot was loaded from (kSettingsSchemaVersion for a
    // fresh/default snapshot; 0 when loaded from a pre-versioning config file).
    int     schemaVersion       = kSettingsSchemaVersion;

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
    bool    reverseScrollDirection = false;

    // Input interoperability (A3, B3):
    float   scrollSensitivity   = 1.0f;  // multiplier on scroll-gesture zoom rate
    bool    momentumZoom        = true;  // allow inertial/momentum scroll to drive zoom

    // Diagnostics: file/debug log verbosity. 0=Debug 1=Info 2=Warn 3=Error —
    // mirrors LogLevel in Logger.h (cast directly). config.json stores the
    // human-friendly string form ("debug"/"info"/"warn"/"error"); an integer
    // 0–3 is also accepted. Default Info: the per-event PTP / Raw-Input paths log
    // at Debug and the logger flushes synchronously per line on the hook-servicing
    // thread, so Debug adds input latency and risks hook deregistration (R-05) —
    // raise it only for a diagnosis session. This is the reliable on-device knob:
    // the brokered UIAccess launch does not inherit SMOOTHZOOM_LOGLEVEL from the
    // environment (see docs/hardware-accommodation-handoff.md §6).
    int     logLevel            = 1;     // LogLevel::Info

    // Phase 4 toggle combo (hardcoded until Phase 5B wires to InputInterceptor)
    int     toggleKey1VK        = 0xA2;  // VK_LCONTROL
    int     toggleKey2VK        = 0xA4;  // VK_LMENU (Alt)
};

class SettingsManager
{
public:
    // Load settings from JSON file. Returns false on missing/corrupt file
    // (AC-2.9.03: defaults remain in effect). Call snapshot() to read values.
    bool loadFromFile(const char* path);

    // Save current settings to JSON file. Creates parent directories if needed.
    bool saveToFile(const char* path) const;

    // Thread-safe snapshot read — no locks. Uses std::atomic_load on shared_ptr.
    std::shared_ptr<const SettingsSnapshot> snapshot() const;

    // Apply a modified snapshot: validates, atomic-swaps, bumps version, notifies observers.
    void applySnapshot(const SettingsSnapshot& newSettings);

    // Default config file path: %AppData%\SmoothZoom\config.json (or $HOME on non-Windows)
    static std::string getDefaultConfigPath();

    // Observer pattern (main thread only, low-frequency).
    // Called synchronously during applySnapshot() and loadFromFile().
    using ChangeCallback = void(*)(const SettingsSnapshot&, void* userData);
    void addObserver(ChangeCallback cb, void* userData);

    // Version counter — render thread compares this for fast change detection.
    uint64_t version() const;

private:
    std::shared_ptr<const SettingsSnapshot> current_ = std::make_shared<SettingsSnapshot>();
    std::atomic<uint64_t> version_{0};

    struct Observer { ChangeCallback cb; void* userData; };
    std::vector<Observer> observers_;
};

} // namespace SmoothZoom

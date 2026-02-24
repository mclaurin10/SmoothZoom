// =============================================================================
// SmoothZoom — SettingsManager
// Config persistence and thread-safe snapshots. Doc 3 §3.9
//
// Phase 5A: JSON load/save with validation, atomic snapshot distribution,
//           observer notification. (AC-2.9.01–AC-2.9.06, AC-2.9.10, AC-2.9.12)
// =============================================================================

#include "smoothzoom/support/SettingsManager.h"

// nlohmann/json — header-only, already linked via CMake
#include <json.hpp>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace SmoothZoom
{

using json = nlohmann::json;

// ─── Config Path ─────────────────────────────────────────────────────────────

std::string SettingsManager::getDefaultConfigPath()
{
#ifdef _WIN32
    const char* appdata = std::getenv("APPDATA");
    if (!appdata || appdata[0] == '\0')
        return {};
    return std::string(appdata) + "\\SmoothZoom\\config.json";
#else
    // Non-Windows fallback (for testing on WSL/Linux)
    const char* home = std::getenv("HOME");
    if (!home || home[0] == '\0')
        return {};
    return std::string(home) + "/.smoothzoom/config.json";
#endif
}

// ─── Load ────────────────────────────────────────────────────────────────────

bool SettingsManager::loadFromFile(const char* path)
{
    std::ifstream file(path);
    if (!file.is_open())
        return false;

    // Parse with no-throw mode (AC-2.9.03: corrupt → return false, defaults stay)
    json j = json::parse(file, nullptr, false);
    if (j.is_discarded())
        return false;

    // Build mutable settings, then freeze as const shared_ptr at the end
    SettingsSnapshot settings; // Start from defaults

    // ── Integer fields ──
    auto readInt = [&](const char* key, int& target, int lo, int hi) {
        if (j.contains(key) && j[key].is_number_integer())
        {
            int v = j[key].get<int>();
            if (v >= lo && v <= hi)
                target = v;
        }
    };

    readInt("modifierKeyVK", settings.modifierKeyVK, 0, 0xFF);
    readInt("animationSpeed", settings.animationSpeed, 0, 2);
    readInt("toggleKey1VK", settings.toggleKey1VK, 0, 0xFF);
    readInt("toggleKey2VK", settings.toggleKey2VK, 0, 0xFF);

    // ── Float fields ──
    auto readFloat = [&](const char* key, float& target, float lo, float hi) {
        if (j.contains(key) && j[key].is_number())
        {
            float v = j[key].get<float>();
            if (v >= lo && v <= hi)
                target = v;
        }
    };

    readFloat("minZoom", settings.minZoom, 1.0f, 10.0f);
    readFloat("maxZoom", settings.maxZoom, 1.0f, 10.0f);
    readFloat("keyboardZoomStep", settings.keyboardZoomStep, 0.05f, 1.0f); // AC-2.9.12: 5%–100%

    // ── Cross-validation: min must be <= max (AC-2.9.10) ──
    if (settings.minZoom > settings.maxZoom)
    {
        settings.minZoom = 1.0f;
        settings.maxZoom = 10.0f;
    }

    // defaultZoomLevel must be within [minZoom, maxZoom]
    readFloat("defaultZoomLevel", settings.defaultZoomLevel, settings.minZoom, settings.maxZoom);

    // ── Boolean fields ──
    auto readBool = [&](const char* key, bool& target) {
        if (j.contains(key) && j[key].is_boolean())
            target = j[key].get<bool>();
    };

    readBool("imageSmoothingEnabled", settings.imageSmoothingEnabled);
    readBool("startWithWindows", settings.startWithWindows);
    readBool("startZoomed", settings.startZoomed);
    readBool("followKeyboardFocus", settings.followKeyboardFocus);
    readBool("followTextCursor", settings.followTextCursor);
    readBool("colorInversionEnabled", settings.colorInversionEnabled);

    // Freeze as const and atomic swap + version bump
    auto snap = std::make_shared<const SettingsSnapshot>(settings);
    std::atomic_store(&current_, snap);
    version_.fetch_add(1, std::memory_order_release);

    // Notify observers
    for (auto& obs : observers_)
        obs.cb(*snap, obs.userData);

    return true;
}

// ─── Save ────────────────────────────────────────────────────────────────────

bool SettingsManager::saveToFile(const char* path) const
{
    auto snap = snapshot();

    json j;
    j["modifierKeyVK"]         = snap->modifierKeyVK;
    j["minZoom"]               = snap->minZoom;
    j["maxZoom"]               = snap->maxZoom;
    j["keyboardZoomStep"]      = snap->keyboardZoomStep;
    j["animationSpeed"]        = snap->animationSpeed;
    j["imageSmoothingEnabled"] = snap->imageSmoothingEnabled;
    j["startWithWindows"]      = snap->startWithWindows;
    j["startZoomed"]           = snap->startZoomed;
    j["defaultZoomLevel"]      = snap->defaultZoomLevel;
    j["followKeyboardFocus"]   = snap->followKeyboardFocus;
    j["followTextCursor"]      = snap->followTextCursor;
    j["colorInversionEnabled"] = snap->colorInversionEnabled;
    j["toggleKey1VK"]          = snap->toggleKey1VK;
    j["toggleKey2VK"]          = snap->toggleKey2VK;

    // Ensure parent directory exists (AC-2.9.01: %AppData%\SmoothZoom\)
    std::error_code ec;
    std::filesystem::path p(path);
    std::filesystem::create_directories(p.parent_path(), ec);
    // Ignore ec — directory may already exist or path may be a bare filename

    std::ofstream file(path);
    if (!file.is_open())
        return false;

    file << j.dump(4); // 4-space indent, human-readable (AC-2.9.01)
    return file.good();
}

// ─── Snapshot Access ─────────────────────────────────────────────────────────

std::shared_ptr<const SettingsSnapshot> SettingsManager::snapshot() const
{
    return std::atomic_load(&current_);
}

// ─── Apply ───────────────────────────────────────────────────────────────────

void SettingsManager::applySnapshot(const SettingsSnapshot& newSettings)
{
    auto snap = std::make_shared<const SettingsSnapshot>(newSettings);
    std::atomic_store(&current_, snap);
    version_.fetch_add(1, std::memory_order_release);

    for (auto& obs : observers_)
        obs.cb(newSettings, obs.userData);
}

// ─── Version ─────────────────────────────────────────────────────────────────

uint64_t SettingsManager::version() const
{
    return version_.load(std::memory_order_acquire);
}

// ─── Observer ────────────────────────────────────────────────────────────────

void SettingsManager::addObserver(ChangeCallback cb, void* userData)
{
    observers_.push_back({cb, userData});
}

} // namespace SmoothZoom

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
#include <cctype>
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
    char* appdata = nullptr;
    size_t len = 0;
    if (_dupenv_s(&appdata, &len, "APPDATA") != 0 || !appdata || appdata[0] == '\0')
    {
        free(appdata);
        return {};
    }
    std::string result = std::string(appdata) + "\\SmoothZoom\\config.json";
    free(appdata);
    return result;
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

    // Schema version: absent ⇒ legacy (pre-versioning) config ⇒ 0, so future
    // migration code can distinguish "written by an older build" from "current".
    settings.schemaVersion = 0;
    readInt("schemaVersion", settings.schemaVersion, 0, 1000000);

    readInt("modifierKeyVK", settings.modifierKeyVK, 0, 0xFF);

    // F-09: Whitelist of valid modifier VK codes. Reject everything else to prevent
    // unusable configurations from crafted config files.
    // Raw VK values to avoid #include <windows.h>:
    //   LWIN=0x5B, RWIN=0x5C, LSHIFT=0xA0, RSHIFT=0xA1, LMENU(Alt)=0xA4, RMENU=0xA5,
    //   SHIFT=0x10, MENU(Alt)=0x12
    // Ctrl family (0x11, 0xA2, 0xA3) deliberately excluded per Ctrl-removal policy.
    {
        int vk = settings.modifierKeyVK;
        bool valid = (vk == 0x5B || vk == 0x5C ||    // LWIN, RWIN
                      vk == 0xA0 || vk == 0xA1 ||    // LSHIFT, RSHIFT
                      vk == 0xA4 || vk == 0xA5 ||    // LMENU, RMENU
                      vk == 0x10 || vk == 0x12);      // SHIFT, MENU
        if (!valid)
            settings.modifierKeyVK = 0x5B;  // Default to LWIN
    }

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
    readFloat("scrollSensitivity", settings.scrollSensitivity, 0.1f, 5.0f); // A3: scroll-gesture rate

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
    readBool("reverseScrollDirection", settings.reverseScrollDirection);
    readBool("momentumZoom", settings.momentumZoom);

    // ── Log level (diagnostics) ──
    // Stored as a human-friendly string ("debug"/"info"/"warn"/"error",
    // case-insensitive); an integer 0–3 is also accepted. Absent or invalid
    // keeps the Info default. Mirrors LogLevel (0=Debug 1=Info 2=Warn 3=Error).
    if (j.contains("logLevel"))
    {
        const auto& lv = j["logLevel"];
        if (lv.is_string())
        {
            std::string s = lv.get<std::string>();
            for (auto& c : s)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if      (s == "debug") settings.logLevel = 0;
            else if (s == "info")  settings.logLevel = 1;
            else if (s == "warn")  settings.logLevel = 2;
            else if (s == "error") settings.logLevel = 3;
            // else: unrecognized → keep default (Info)
        }
        else if (lv.is_number_integer())
        {
            int v = lv.get<int>();
            if (v >= 0 && v <= 3)
                settings.logLevel = v;
        }
    }

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
    // Always stamp the CURRENT schema version (not snap->schemaVersion): saving
    // migrates a legacy file forward to the current schema on the next write.
    j["schemaVersion"]         = kSettingsSchemaVersion;
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
    j["reverseScrollDirection"] = snap->reverseScrollDirection;
    j["scrollSensitivity"]     = snap->scrollSensitivity;
    j["momentumZoom"]          = snap->momentumZoom;
    // logLevel written as a human-readable string (mirrors the load mapping).
    j["logLevel"]              = (snap->logLevel == 0) ? "debug" :
                                 (snap->logLevel == 2) ? "warn"  :
                                 (snap->logLevel == 3) ? "error" : "info";
    j["toggleKey1VK"]          = snap->toggleKey1VK;
    j["toggleKey2VK"]          = snap->toggleKey2VK;

    // Ensure parent directory exists (AC-2.9.01: %AppData%\SmoothZoom\)
    std::error_code ec;
    std::filesystem::path p(path);
    std::filesystem::create_directories(p.parent_path(), ec);
    // Ignore ec — directory may already exist or path may be a bare filename

    // Atomic write (R-14): serialize to a sibling temp file, flush it fully, then
    // rename over the destination. A crash, power loss, or full disk mid-write can
    // only damage the temp file — the live config.json is swapped in by a single
    // same-directory rename (atomic on NTFS), so loadFromFile never observes a
    // truncated/half-written file and silently falls back to defaults.
    std::filesystem::path tmpPath(std::string(path) + ".tmp");
    {
        std::ofstream file(tmpPath);
        if (!file.is_open())
            return false;

        file << j.dump(4); // 4-space indent, human-readable (AC-2.9.01)

        // Close explicitly and re-check AFTER close. A disk-full / I/O error can
        // surface only when the stream's buffer is flushed on close(), which is
        // after a plain good() check would already have passed. Committing the
        // rename of a truncated temp over the good config would re-introduce the
        // exact corruption this guards against, so verify the stream survived the
        // close before renaming.
        file.close();
        if (!file)
        {
            std::filesystem::remove(tmpPath, ec); // best-effort cleanup
            return false;
        }
    } // ofstream destroyed here; handle already released by the explicit close()

    std::filesystem::rename(tmpPath, p, ec);
    if (ec)
    {
        std::filesystem::remove(tmpPath, ec); // best-effort cleanup; keep old config intact
        return false;
    }
    return true;
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

// =============================================================================
// Unit tests for SettingsManager — Doc 3 §3.9
// Pure logic — no Win32 API dependencies.
//
// Phase 5A: JSON persistence, validation, corruption recovery, observer pattern.
// =============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "smoothzoom/support/SettingsManager.h"

#include <cstdio>
#include <fstream>
#include <string>

using namespace SmoothZoom;
using Catch::Approx;

// Helper: write a string to a temp file and return the path
static std::string writeTempFile(const std::string& content, const char* name = "test_config.json")
{
    std::string path = std::string("/tmp/smoothzoom_test_") + name;
    std::ofstream f(path);
    f << content;
    f.close();
    return path;
}

// Helper: read a file to string
static std::string readFileContents(const std::string& path)
{
    std::ifstream f(path);
    return std::string((std::istreambuf_iterator<char>(f)),
                        std::istreambuf_iterator<char>());
}

TEST_CASE("SettingsManager starts with defaults", "[SettingsManager][Phase5]")
{
    SettingsManager mgr;
    auto snap = mgr.snapshot();

    REQUIRE(snap->modifierKeyVK == 0x5B);
    REQUIRE(snap->minZoom == Approx(1.0f));
    REQUIRE(snap->maxZoom == Approx(10.0f));
    REQUIRE(snap->keyboardZoomStep == Approx(0.25f));
    REQUIRE(snap->animationSpeed == 1);
    REQUIRE(snap->imageSmoothingEnabled == true);
    REQUIRE(snap->startWithWindows == false);
    REQUIRE(snap->startZoomed == false);
    REQUIRE(snap->defaultZoomLevel == Approx(2.0f));
    REQUIRE(snap->followKeyboardFocus == true);
    REQUIRE(snap->followTextCursor == true);
    REQUIRE(snap->colorInversionEnabled == false);
    REQUIRE(snap->toggleKey1VK == 0xA2);
    REQUIRE(snap->toggleKey2VK == 0xA4);
}

TEST_CASE("Load valid JSON → all fields parsed (AC-2.9.01)", "[SettingsManager][Phase5]")
{
    std::string json = R"({
        "modifierKeyVK": 162,
        "minZoom": 1.5,
        "maxZoom": 8.0,
        "keyboardZoomStep": 0.5,
        "animationSpeed": 2,
        "imageSmoothingEnabled": false,
        "startWithWindows": true,
        "startZoomed": true,
        "defaultZoomLevel": 3.0,
        "followKeyboardFocus": false,
        "followTextCursor": false,
        "colorInversionEnabled": true,
        "toggleKey1VK": 160,
        "toggleKey2VK": 164
    })";

    auto path = writeTempFile(json);
    SettingsManager mgr;
    REQUIRE(mgr.loadFromFile(path.c_str()));

    auto snap = mgr.snapshot();
    REQUIRE(snap->modifierKeyVK == 162);
    REQUIRE(snap->minZoom == Approx(1.5f));
    REQUIRE(snap->maxZoom == Approx(8.0f));
    REQUIRE(snap->keyboardZoomStep == Approx(0.5f));
    REQUIRE(snap->animationSpeed == 2);
    REQUIRE(snap->imageSmoothingEnabled == false);
    REQUIRE(snap->startWithWindows == true);
    REQUIRE(snap->startZoomed == true);
    REQUIRE(snap->defaultZoomLevel == Approx(3.0f));
    REQUIRE(snap->followKeyboardFocus == false);
    REQUIRE(snap->followTextCursor == false);
    REQUIRE(snap->colorInversionEnabled == true);
    REQUIRE(snap->toggleKey1VK == 160);
    REQUIRE(snap->toggleKey2VK == 164);
}

TEST_CASE("Save then load round-trip (AC-2.9.02)", "[SettingsManager][Phase5]")
{
    // Set non-default values
    SettingsSnapshot custom;
    custom.modifierKeyVK = 0xA2;
    custom.minZoom = 1.5f;
    custom.maxZoom = 7.0f;
    custom.keyboardZoomStep = 0.1f;
    custom.animationSpeed = 0;
    custom.imageSmoothingEnabled = false;
    custom.startWithWindows = true;
    custom.startZoomed = true;
    custom.defaultZoomLevel = 4.0f;
    custom.followKeyboardFocus = false;
    custom.followTextCursor = false;
    custom.colorInversionEnabled = true;
    custom.toggleKey1VK = 0xA0;
    custom.toggleKey2VK = 0xA5;

    SettingsManager mgr1;
    mgr1.applySnapshot(custom);

    std::string path = "/tmp/smoothzoom_test_roundtrip.json";
    REQUIRE(mgr1.saveToFile(path.c_str()));

    SettingsManager mgr2;
    REQUIRE(mgr2.loadFromFile(path.c_str()));

    auto snap = mgr2.snapshot();
    REQUIRE(snap->modifierKeyVK == custom.modifierKeyVK);
    REQUIRE(snap->minZoom == Approx(custom.minZoom));
    REQUIRE(snap->maxZoom == Approx(custom.maxZoom));
    REQUIRE(snap->keyboardZoomStep == Approx(custom.keyboardZoomStep));
    REQUIRE(snap->animationSpeed == custom.animationSpeed);
    REQUIRE(snap->imageSmoothingEnabled == custom.imageSmoothingEnabled);
    REQUIRE(snap->startWithWindows == custom.startWithWindows);
    REQUIRE(snap->startZoomed == custom.startZoomed);
    REQUIRE(snap->defaultZoomLevel == Approx(custom.defaultZoomLevel));
    REQUIRE(snap->followKeyboardFocus == custom.followKeyboardFocus);
    REQUIRE(snap->followTextCursor == custom.followTextCursor);
    REQUIRE(snap->colorInversionEnabled == custom.colorInversionEnabled);
    REQUIRE(snap->toggleKey1VK == custom.toggleKey1VK);
    REQUIRE(snap->toggleKey2VK == custom.toggleKey2VK);
}

TEST_CASE("Load corrupt JSON → returns false, defaults intact (AC-2.9.03, E5.5)", "[SettingsManager][Phase5]")
{
    auto path = writeTempFile("{invalid json!!!", "corrupt.json");

    SettingsManager mgr;
    REQUIRE_FALSE(mgr.loadFromFile(path.c_str()));

    // Snapshot should still have defaults
    auto snap = mgr.snapshot();
    REQUIRE(snap->maxZoom == Approx(10.0f));
    REQUIRE(snap->minZoom == Approx(1.0f));
}

TEST_CASE("Load missing file → returns false, defaults intact", "[SettingsManager][Phase5]")
{
    SettingsManager mgr;
    REQUIRE_FALSE(mgr.loadFromFile("/tmp/smoothzoom_nonexistent_12345.json"));

    auto snap = mgr.snapshot();
    REQUIRE(snap->maxZoom == Approx(10.0f));
}

TEST_CASE("Load JSON with missing fields → defaults fill in (AC-2.9.02)", "[SettingsManager][Phase5]")
{
    // Only specify maxZoom — everything else should be default
    auto path = writeTempFile(R"({"maxZoom": 5.0})", "partial.json");

    SettingsManager mgr;
    REQUIRE(mgr.loadFromFile(path.c_str()));

    auto snap = mgr.snapshot();
    REQUIRE(snap->maxZoom == Approx(5.0f));          // Changed
    REQUIRE(snap->minZoom == Approx(1.0f));           // Default
    REQUIRE(snap->keyboardZoomStep == Approx(0.25f)); // Default
    REQUIRE(snap->modifierKeyVK == 0x5B);             // Default
    REQUIRE(snap->defaultZoomLevel == Approx(2.0f));  // Default (within [1.0, 5.0])
}

TEST_CASE("Load with out-of-range keyboardZoomStep → clamped (AC-2.9.12)", "[SettingsManager][Phase5]")
{
    // Step of 0.01 (1%) is below minimum 0.05 (5%)
    auto path1 = writeTempFile(R"({"keyboardZoomStep": 0.01})", "step_low.json");
    SettingsManager mgr1;
    REQUIRE(mgr1.loadFromFile(path1.c_str()));
    REQUIRE(mgr1.snapshot()->keyboardZoomStep == Approx(0.25f)); // Kept default

    // Step of 2.0 (200%) is above maximum 1.0 (100%)
    auto path2 = writeTempFile(R"({"keyboardZoomStep": 2.0})", "step_high.json");
    SettingsManager mgr2;
    REQUIRE(mgr2.loadFromFile(path2.c_str()));
    REQUIRE(mgr2.snapshot()->keyboardZoomStep == Approx(0.25f)); // Kept default
}

TEST_CASE("Load with minZoom > maxZoom → both reset to defaults (AC-2.9.10)", "[SettingsManager][Phase5]")
{
    auto path = writeTempFile(R"({"minZoom": 8.0, "maxZoom": 3.0})", "inverted.json");

    SettingsManager mgr;
    REQUIRE(mgr.loadFromFile(path.c_str()));

    auto snap = mgr.snapshot();
    REQUIRE(snap->minZoom == Approx(1.0f));
    REQUIRE(snap->maxZoom == Approx(10.0f));
}

TEST_CASE("Load with out-of-range zoom bounds → clamped", "[SettingsManager][Phase5]")
{
    // minZoom below 1.0 → keeps default
    auto path = writeTempFile(R"({"minZoom": 0.5, "maxZoom": 15.0})", "bounds.json");

    SettingsManager mgr;
    REQUIRE(mgr.loadFromFile(path.c_str()));

    auto snap = mgr.snapshot();
    REQUIRE(snap->minZoom == Approx(1.0f));  // 0.5 out of range → default
    REQUIRE(snap->maxZoom == Approx(10.0f)); // 15.0 out of range → default
}

TEST_CASE("applySnapshot → version increments", "[SettingsManager][Phase5]")
{
    SettingsManager mgr;
    uint64_t v0 = mgr.version();

    SettingsSnapshot snap;
    snap.maxZoom = 5.0f;
    mgr.applySnapshot(snap);

    REQUIRE(mgr.version() > v0);
    REQUIRE(mgr.snapshot()->maxZoom == Approx(5.0f));
}

TEST_CASE("loadFromFile → version increments", "[SettingsManager][Phase5]")
{
    auto path = writeTempFile(R"({"maxZoom": 6.0})", "version.json");

    SettingsManager mgr;
    uint64_t v0 = mgr.version();

    REQUIRE(mgr.loadFromFile(path.c_str()));
    REQUIRE(mgr.version() > v0);
}

TEST_CASE("applySnapshot → observer called", "[SettingsManager][Phase5]")
{
    SettingsManager mgr;

    bool called = false;
    float observedMax = 0.0f;
    mgr.addObserver([](const SettingsSnapshot& snap, void* ud) {
        auto* calledPtr = static_cast<bool*>(ud);
        *calledPtr = true;
    }, &called);

    SettingsSnapshot snap;
    snap.maxZoom = 7.0f;
    mgr.applySnapshot(snap);

    REQUIRE(called);
}

TEST_CASE("snapshot() returns thread-safe copy", "[SettingsManager][Phase5]")
{
    SettingsManager mgr;

    auto snap1 = mgr.snapshot();
    REQUIRE(snap1->maxZoom == Approx(10.0f));

    // Apply new settings
    SettingsSnapshot newSnap;
    newSnap.maxZoom = 5.0f;
    mgr.applySnapshot(newSnap);

    // snap1 should be unchanged (immutable copy)
    REQUIRE(snap1->maxZoom == Approx(10.0f));

    // New snapshot reflects change
    auto snap2 = mgr.snapshot();
    REQUIRE(snap2->maxZoom == Approx(5.0f));
}

TEST_CASE("defaultZoomLevel out of range for custom bounds → keeps default", "[SettingsManager][Phase5]")
{
    // maxZoom=3.0, defaultZoomLevel=5.0 (above max) → default should stay 2.0
    auto path = writeTempFile(R"({"maxZoom": 3.0, "defaultZoomLevel": 5.0})", "deflevel.json");

    SettingsManager mgr;
    REQUIRE(mgr.loadFromFile(path.c_str()));

    auto snap = mgr.snapshot();
    REQUIRE(snap->maxZoom == Approx(3.0f));
    REQUIRE(snap->defaultZoomLevel == Approx(2.0f)); // 5.0 > 3.0 → kept default
}

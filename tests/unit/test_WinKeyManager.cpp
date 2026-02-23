// =============================================================================
// Unit tests for WinKeyManager — Doc 3 §3.2
// Pure logic — no Win32 API dependencies.
// =============================================================================

#include <catch2/catch_test_macros.hpp>
#include "smoothzoom/input/WinKeyManager.h"

using namespace SmoothZoom;

TEST_CASE("WinKeyManager starts in Idle state", "[WinKeyManager]")
{
    WinKeyManager wkm;
    REQUIRE(wkm.state() == WinKeyManager::State::Idle);
    REQUIRE_FALSE(wkm.shouldSuppressStartMenu());
}

TEST_CASE("Win key down transitions to HeldClean", "[WinKeyManager]")
{
    WinKeyManager wkm;
    wkm.onWinKeyDown();
    REQUIRE(wkm.state() == WinKeyManager::State::HeldClean);
    REQUIRE_FALSE(wkm.shouldSuppressStartMenu());
}

TEST_CASE("markUsedForZoom transitions HeldClean to HeldUsed", "[WinKeyManager]")
{
    WinKeyManager wkm;
    wkm.onWinKeyDown();
    wkm.markUsedForZoom();
    REQUIRE(wkm.state() == WinKeyManager::State::HeldUsed);
    REQUIRE(wkm.shouldSuppressStartMenu());
}

TEST_CASE("Win key up returns to Idle", "[WinKeyManager]")
{
    WinKeyManager wkm;
    wkm.onWinKeyDown();
    wkm.markUsedForZoom();
    wkm.onWinKeyUp();
    REQUIRE(wkm.state() == WinKeyManager::State::Idle);
}

TEST_CASE("markUsedForZoom is no-op in Idle state", "[WinKeyManager]")
{
    WinKeyManager wkm;
    wkm.markUsedForZoom();
    REQUIRE(wkm.state() == WinKeyManager::State::Idle);
}

TEST_CASE("Win key release without scroll does NOT suppress (AC-2.1.17)", "[WinKeyManager]")
{
    WinKeyManager wkm;
    wkm.onWinKeyDown();
    // No markUsedForZoom — user didn't scroll
    REQUIRE_FALSE(wkm.shouldSuppressStartMenu());
    wkm.onWinKeyUp();
    REQUIRE(wkm.state() == WinKeyManager::State::Idle);
}

TEST_CASE("Multiple Win key down calls don't reset HeldUsed", "[WinKeyManager]")
{
    WinKeyManager wkm;
    wkm.onWinKeyDown();
    wkm.markUsedForZoom();
    REQUIRE(wkm.state() == WinKeyManager::State::HeldUsed);

    // Second key-down while already held should not change state
    wkm.onWinKeyDown();
    REQUIRE(wkm.state() == WinKeyManager::State::HeldUsed);
}

TEST_CASE("Full cycle: down, used, up, down (fresh start)", "[WinKeyManager]")
{
    WinKeyManager wkm;

    // First press: zoom
    wkm.onWinKeyDown();
    wkm.markUsedForZoom();
    REQUIRE(wkm.shouldSuppressStartMenu());
    wkm.onWinKeyUp();

    // Second press: no zoom
    wkm.onWinKeyDown();
    REQUIRE(wkm.state() == WinKeyManager::State::HeldClean);
    REQUIRE_FALSE(wkm.shouldSuppressStartMenu());
}

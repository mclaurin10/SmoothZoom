// =============================================================================
// Unit tests for ModifierUtils — isModifierMatch and toGenericVK
// Pure logic — no Win32 API dependencies.
// =============================================================================

#include <catch2/catch_test_macros.hpp>
#include "smoothzoom/input/ModifierUtils.h"

using namespace SmoothZoom;

// =============================================================================
// isModifierMatch — L/R equivalence
// =============================================================================

TEST_CASE("isModifierMatch: Ctrl family matches both sides", "[ModifierUtils]")
{
    // Configured as VK_LCONTROL
    REQUIRE(isModifierMatch(VK_LCONTROL, VK_LCONTROL));
    REQUIRE(isModifierMatch(VK_RCONTROL, VK_LCONTROL));

    // Configured as VK_RCONTROL
    REQUIRE(isModifierMatch(VK_LCONTROL, VK_RCONTROL));
    REQUIRE(isModifierMatch(VK_RCONTROL, VK_RCONTROL));

    // Configured as generic VK_CONTROL
    REQUIRE(isModifierMatch(VK_LCONTROL, VK_CONTROL));
    REQUIRE(isModifierMatch(VK_RCONTROL, VK_CONTROL));
}

TEST_CASE("isModifierMatch: Alt family matches both sides", "[ModifierUtils]")
{
    REQUIRE(isModifierMatch(VK_LMENU, VK_LMENU));
    REQUIRE(isModifierMatch(VK_RMENU, VK_LMENU));
    REQUIRE(isModifierMatch(VK_LMENU, VK_RMENU));
    REQUIRE(isModifierMatch(VK_RMENU, VK_RMENU));
    REQUIRE(isModifierMatch(VK_LMENU, VK_MENU));
    REQUIRE(isModifierMatch(VK_RMENU, VK_MENU));
}

TEST_CASE("isModifierMatch: Shift family matches both sides", "[ModifierUtils]")
{
    REQUIRE(isModifierMatch(VK_LSHIFT, VK_LSHIFT));
    REQUIRE(isModifierMatch(VK_RSHIFT, VK_LSHIFT));
    REQUIRE(isModifierMatch(VK_LSHIFT, VK_SHIFT));
    REQUIRE(isModifierMatch(VK_RSHIFT, VK_SHIFT));
}

TEST_CASE("isModifierMatch: Win key matches both sides", "[ModifierUtils]")
{
    REQUIRE(isModifierMatch(VK_LWIN, VK_LWIN));
    REQUIRE(isModifierMatch(VK_RWIN, VK_LWIN));
    REQUIRE(isModifierMatch(VK_LWIN, VK_RWIN));
    REQUIRE(isModifierMatch(VK_RWIN, VK_RWIN));
}

TEST_CASE("isModifierMatch: cross-family rejection", "[ModifierUtils]")
{
    REQUIRE_FALSE(isModifierMatch(VK_LMENU, VK_LCONTROL));
    REQUIRE_FALSE(isModifierMatch(VK_LCONTROL, VK_LMENU));
    REQUIRE_FALSE(isModifierMatch(VK_LSHIFT, VK_LCONTROL));
    REQUIRE_FALSE(isModifierMatch(VK_LWIN, VK_LCONTROL));
    REQUIRE_FALSE(isModifierMatch(VK_LCONTROL, VK_LWIN));
}

TEST_CASE("isModifierMatch: non-modifier key exact match", "[ModifierUtils]")
{
    REQUIRE(isModifierMatch('M', 'M'));
    REQUIRE_FALSE(isModifierMatch('M', 'N'));
    REQUIRE(isModifierMatch(0x41, 0x41)); // 'A'
}

// =============================================================================
// toGenericVK
// =============================================================================

TEST_CASE("toGenericVK: Ctrl variants → VK_CONTROL", "[ModifierUtils]")
{
    REQUIRE(toGenericVK(VK_LCONTROL) == VK_CONTROL);
    REQUIRE(toGenericVK(VK_RCONTROL) == VK_CONTROL);
}

TEST_CASE("toGenericVK: Alt variants → VK_MENU", "[ModifierUtils]")
{
    REQUIRE(toGenericVK(VK_LMENU) == VK_MENU);
    REQUIRE(toGenericVK(VK_RMENU) == VK_MENU);
}

TEST_CASE("toGenericVK: Shift variants → VK_SHIFT", "[ModifierUtils]")
{
    REQUIRE(toGenericVK(VK_LSHIFT) == VK_SHIFT);
    REQUIRE(toGenericVK(VK_RSHIFT) == VK_SHIFT);
}

TEST_CASE("toGenericVK: non-modifier passthrough", "[ModifierUtils]")
{
    REQUIRE(toGenericVK('M') == 'M');
    REQUIRE(toGenericVK(VK_LWIN) == VK_LWIN);
    REQUIRE(toGenericVK(0x41) == 0x41);
}

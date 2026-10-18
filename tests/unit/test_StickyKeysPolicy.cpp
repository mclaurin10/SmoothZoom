// =============================================================================
// Unit tests — StickyKeysPolicy (suppress the 5×Shift Sticky Keys shortcut
// while Shift is the scroll modifier). AC-2.1.23, AC-2.1.24, R-31.
// =============================================================================

#include <catch2/catch_test_macros.hpp>
#include "smoothzoom/input/StickyKeysPolicy.h"

using namespace SmoothZoom;

// Windows 10/11 default "Flags" value: shortcut on with confirmation.
static constexpr uint32_t kDefault510 = 510;   // 0x1FE
static constexpr uint32_t kShortcutOff506 = 506; // 0x1FA, hotkey bit cleared

TEST_CASE("Suppression is wanted only with the setting on and Shift as the modifier", "[StickyKeys]")
{
    for (int vk : { VK_LSHIFT, VK_RSHIFT, VK_SHIFT })
    {
        CHECK(stickyShortcutSuppressionWanted(true, vk));
        CHECK_FALSE(stickyShortcutSuppressionWanted(false, vk));
    }
    for (int vk : { VK_LWIN, VK_RWIN, VK_LMENU, VK_RMENU, VK_MENU })
    {
        CHECK_FALSE(stickyShortcutSuppressionWanted(true, vk));
        CHECK_FALSE(stickyShortcutSuppressionWanted(false, vk));
    }
}

TEST_CASE("Suppressing clears only the two shortcut bits", "[StickyKeys]")
{
    REQUIRE((kDefault510 & kSkfHotkeyActive) != 0);
    const uint32_t s = suppressedStickyFlags(kDefault510);
    CHECK((s & kSkfHotkeyActive) == 0);
    CHECK((s & kSkfConfirmHotkey) == 0);
    CHECK((s | kSkfShortcutBits) == (kDefault510 | kSkfShortcutBits)); // nothing else changed
    CHECK(suppressedStickyFlags(0xFFFFFFFFu) == (0xFFFFFFFFu & ~kSkfShortcutBits));
}

TEST_CASE("Never touches the flags while Sticky Keys itself is on", "[StickyKeys]")
{
    CHECK(stickyKeysMayBeChanged(kDefault510));
    CHECK_FALSE(stickyKeysMayBeChanged(kDefault510 | kSkfStickyKeysOn));
}

TEST_CASE("Restore puts back the user's shortcut bits and keeps other current bits", "[StickyKeys]")
{
    const uint32_t during = suppressedStickyFlags(kDefault510);
    CHECK(restoredStickyFlags(during, kDefault510) == kDefault510);

    // The user had the shortcut off already: restore gives back exactly their value
    // (hotkey bit still off; their confirm bit untouched).
    CHECK(restoredStickyFlags(suppressedStickyFlags(kShortcutOff506), kShortcutOff506) == kShortcutOff506);
    CHECK((restoredStickyFlags(suppressedStickyFlags(kShortcutOff506), kShortcutOff506) & kSkfHotkeyActive) == 0);

    // Another bit changed meanwhile (e.g. Sticky Keys turned on in Settings): kept.
    const uint32_t changed = during | kSkfStickyKeysOn;
    CHECK(restoredStickyFlags(changed, kDefault510) == (kDefault510 | kSkfStickyKeysOn));
}

TEST_CASE("Killed-run repair: restoring from the persisted value turns the shortcut back on", "[StickyKeys]")
{
    // A previous run was killed while suppressing: the in-memory flags still have
    // the shortcut off, but the profile (never written by SmoothZoom) has it on.
    const uint32_t inMemoryAfterKill = suppressedStickyFlags(kDefault510);
    CHECK(restoredStickyFlags(inMemoryAfterKill, kDefault510) == kDefault510);
}

TEST_CASE("parseStickyFlagsString accepts plain decimals only", "[StickyKeys]")
{
    uint32_t v = 0;
    CHECK(parseStickyFlagsString(L"510", v));
    CHECK(v == 510u);
    CHECK(parseStickyFlagsString(L"0", v));
    CHECK(v == 0u);
    CHECK(parseStickyFlagsString(L"4294967295", v));
    CHECK(v == 0xFFFFFFFFu);
    CHECK_FALSE(parseStickyFlagsString(L"4294967296", v));
    CHECK_FALSE(parseStickyFlagsString(L"", v));
    CHECK_FALSE(parseStickyFlagsString(nullptr, v));
    CHECK_FALSE(parseStickyFlagsString(L"-1", v));
    CHECK_FALSE(parseStickyFlagsString(L"0x1FE", v));
    CHECK_FALSE(parseStickyFlagsString(L"510 ", v));
}

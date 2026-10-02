// =============================================================================
// Unit tests — KeyDecision (the WH_KEYBOARD_LL zoom-shortcut decision, pure).
// AC-2.8.01–AC-2.8.09, AC-2.1.16/20, the character-leak fix (Shift+= / Shift+-),
// and the "Keyboard shortcuts" setting (AC-2.8.13–AC-2.8.15).
// =============================================================================

#include <catch2/catch_test_macros.hpp>
#include "smoothzoom/input/KeyDecision.h"

using namespace SmoothZoom;

namespace
{
const int kZoomKeys[] = { VK_OEM_PLUS, VK_ADD, VK_OEM_MINUS, VK_SUBTRACT };
const int kModifiers[] = { VK_LSHIFT, VK_LWIN, VK_LMENU };
const int kModifierKeyVKs[] = { VK_SHIFT, VK_LSHIFT, VK_RSHIFT, VK_CONTROL, VK_LCONTROL,
                                VK_RCONTROL, VK_MENU, VK_LMENU, VK_RMENU, VK_LWIN, VK_RWIN };

// Every key the tests sweep: the four zoom keys, Esc, the modifiers, and ordinary keys.
const int kAllKeys[] = { VK_OEM_PLUS, VK_ADD, VK_OEM_MINUS, VK_SUBTRACT, VK_ESCAPE,
                         VK_SHIFT, VK_LSHIFT, VK_RSHIFT, VK_CONTROL, VK_LCONTROL,
                         VK_RCONTROL, VK_MENU, VK_LMENU, VK_RMENU, VK_LWIN, VK_RWIN,
                         'A', 'M', 'I', '0', 0x20 /* space */, 0x0D /* enter */ };
} // namespace

TEST_CASE("Shortcuts on: zoom keys post a command on key-down only (AC-2.8.01/02)", "[KeyDecision]")
{
    for (int mod : kModifiers)
    {
        CHECK(decideShortcutKey(VK_OEM_PLUS, true, false, false, mod, true, true).action == ShortcutAction::ZoomIn);
        CHECK(decideShortcutKey(VK_ADD, true, false, false, mod, true, true).action == ShortcutAction::ZoomIn);
        CHECK(decideShortcutKey(VK_OEM_MINUS, true, false, false, mod, true, true).action == ShortcutAction::ZoomOut);
        CHECK(decideShortcutKey(VK_SUBTRACT, true, false, false, mod, true, true).action == ShortcutAction::ZoomOut);
        CHECK(decideShortcutKey(VK_ESCAPE, true, false, false, mod, true, true).action == ShortcutAction::ResetZoom);

        for (int vk : kZoomKeys)
            CHECK(decideShortcutKey(vk, false, true, false, mod, true, true).action == ShortcutAction::None);
        CHECK(decideShortcutKey(VK_ESCAPE, false, true, false, mod, true, true).action == ShortcutAction::None);
    }
}

TEST_CASE("Shortcuts on: only the four zoom keys are consumed, on both edges, while the modifier is held", "[KeyDecision]")
{
    for (int mod : kModifiers)
        for (int vk : kAllKeys)
            for (bool injected : { false, true })
            {
                const bool expected = isZoomStepKey(vk);
                INFO("mod=" << mod << " vk=" << vk << " injected=" << injected);
                CHECK(decideShortcutKey(vk, true, false, injected, mod, true, true).consume == expected);
                CHECK(decideShortcutKey(vk, false, true, injected, mod, true, true).consume == expected);
            }
}

TEST_CASE("Shortcuts on: injected zoom keys (Options+ macros) are consumed and act like physical ones", "[KeyDecision]")
{
    for (int vk : kZoomKeys)
    {
        KeyDecision phys = decideShortcutKey(vk, true, false, false, VK_LSHIFT, true, true);
        KeyDecision inj  = decideShortcutKey(vk, true, false, true,  VK_LSHIFT, true, true);
        CHECK(inj.consume);
        CHECK(inj.consume == phys.consume);
        CHECK(inj.action == phys.action);
        CHECK(decideShortcutKey(vk, false, true, true, VK_LSHIFT, true, true).consume);
    }
}

TEST_CASE("Shortcuts on: nothing is consumed or posted when the modifier is not held", "[KeyDecision]")
{
    for (int mod : kModifiers)
        for (int vk : kAllKeys)
            for (bool injected : { false, true })
            {
                KeyDecision down = decideShortcutKey(vk, true, false, injected, mod, false, true);
                KeyDecision up   = decideShortcutKey(vk, false, true, injected, mod, false, true);
                CHECK_FALSE(down.consume);
                CHECK_FALSE(up.consume);
                CHECK(down.action == ShortcutAction::None);
                CHECK_FALSE(down.markWinUsedForZoom);
            }
}

TEST_CASE("Shortcuts off: every key passes through and nothing is posted (AC-2.8.14)", "[KeyDecision]")
{
    for (int mod : kModifiers)
        for (int vk : kAllKeys)
            for (bool injected : { false, true })
                for (bool held : { false, true })
                {
                    INFO("mod=" << mod << " vk=" << vk << " injected=" << injected << " held=" << held);
                    KeyDecision down = decideShortcutKey(vk, true, false, injected, mod, held, false);
                    KeyDecision up   = decideShortcutKey(vk, false, true, injected, mod, held, false);
                    CHECK_FALSE(down.consume);
                    CHECK_FALSE(up.consume);
                    CHECK(down.action == ShortcutAction::None);
                    CHECK_FALSE(down.markWinUsedForZoom);
                }
}

TEST_CASE("Shift key events are never consumed, in any state (AC-2.1.22)", "[KeyDecision]")
{
    for (int shiftVK : { VK_SHIFT, VK_LSHIFT, VK_RSHIFT })
        for (int mod : kModifiers)
            for (bool injected : { false, true })
                for (bool held : { false, true })
                    for (bool enabled : { false, true })
                    {
                        CHECK_FALSE(decideShortcutKey(shiftVK, true, false, injected, mod, held, enabled).consume);
                        CHECK_FALSE(decideShortcutKey(shiftVK, false, true, injected, mod, held, enabled).consume);
                    }
}

TEST_CASE("No modifier key is ever consumed or treated as a shortcut", "[KeyDecision]")
{
    for (int vk : kModifierKeyVKs)
        for (int mod : kModifiers)
        {
            KeyDecision d = decideShortcutKey(vk, true, false, false, mod, true, true);
            CHECK_FALSE(d.consume);
            CHECK(d.action == ShortcutAction::None);
        }
}

TEST_CASE("Esc reset is posted but never consumed (AC-2.8.08)", "[KeyDecision]")
{
    KeyDecision d = decideShortcutKey(VK_ESCAPE, true, false, false, VK_LSHIFT, true, true);
    CHECK(d.action == ShortcutAction::ResetZoom);
    CHECK_FALSE(d.consume);
}

TEST_CASE("Start-menu suppression flag only with the Win modifier (AC-2.1.16, AC-2.1.20)", "[KeyDecision]")
{
    CHECK(decideShortcutKey(VK_OEM_PLUS, true, false, false, VK_LWIN, true, true).markWinUsedForZoom);
    CHECK(decideShortcutKey(VK_ESCAPE, true, false, false, VK_RWIN, true, true).markWinUsedForZoom);
    for (int vk : { VK_OEM_PLUS, VK_ADD, VK_OEM_MINUS, VK_SUBTRACT, VK_ESCAPE })
    {
        CHECK_FALSE(decideShortcutKey(vk, true, false, false, VK_LSHIFT, true, true).markWinUsedForZoom);
        CHECK_FALSE(decideShortcutKey(vk, true, false, false, VK_LMENU, true, true).markWinUsedForZoom);
    }
    // Key-up of a Win chord posts nothing, so it does not mark either.
    CHECK_FALSE(decideShortcutKey(VK_OEM_PLUS, false, true, false, VK_LWIN, true, true).markWinUsedForZoom);
}

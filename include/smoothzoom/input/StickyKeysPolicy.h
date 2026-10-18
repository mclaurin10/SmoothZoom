#pragma once
// =============================================================================
// SmoothZoom — StickyKeysPolicy
// Pure decisions for suppressing the Sticky Keys *shortcut* (Shift pressed five
// times opens the "Turn on Sticky Keys?" prompt) while Shift is the scroll
// modifier (AC-2.1.23, AC-2.1.24, R-31). InputInterceptor applies them with
// SystemParametersInfo(SPI_GET/SETSTICKYKEYS) on the main thread, never in a
// hook callback. Header-only, no Win32 dependency, unit-tested.
//
// Microsoft's "Disabling Shortcut Keys in Games" pattern, narrowed:
//   - only the two shortcut bits change (SKF_HOTKEYACTIVE, SKF_CONFIRMHOTKEY);
//   - never while Sticky Keys itself is ON (the user relies on it);
//   - never persisted to the user profile (fWinIni = 0), so a sign-out always
//     restores the user's own setting;
//   - restored from the user's persisted profile value, which also repairs a
//     previous run that was killed before it could restore.
// =============================================================================

#include "smoothzoom/input/ModifierUtils.h"
#include <cstdint>

namespace SmoothZoom
{

// STICKYKEYS::dwFlags bits (WinUser.h values; raw so this header needs no Win32).
inline constexpr uint32_t kSkfStickyKeysOn  = 0x00000001;  // SKF_STICKYKEYSON
inline constexpr uint32_t kSkfHotkeyActive  = 0x00000004;  // SKF_HOTKEYACTIVE
inline constexpr uint32_t kSkfConfirmHotkey = 0x00000008;  // SKF_CONFIRMHOTKEY
inline constexpr uint32_t kSkfShortcutBits  = kSkfHotkeyActive | kSkfConfirmHotkey;

// Suppression is wanted only when the setting is on AND Shift is the modifier:
// with Win or Alt, SmoothZoom does not cause repeated Shift presses.
inline bool stickyShortcutSuppressionWanted(bool settingOn, int modifierVK)
{
    return settingOn && isShiftModifierVK(modifierVK);
}

// Whether SmoothZoom may touch the flags at all right now.
inline bool stickyKeysMayBeChanged(uint32_t currentFlags)
{
    return (currentFlags & kSkfStickyKeysOn) == 0;
}

// Flags to set while suppressing: the shortcut bits cleared, everything else kept.
inline uint32_t suppressedStickyFlags(uint32_t currentFlags)
{
    return currentFlags & ~kSkfShortcutBits;
}

// Flags to set when restoring: the current flags (the user may have changed
// other bits meanwhile) with the shortcut bits taken from the user's own value.
inline uint32_t restoredStickyFlags(uint32_t currentFlags, uint32_t userFlags)
{
    return (currentFlags & ~kSkfShortcutBits) | (userFlags & kSkfShortcutBits);
}

// Parses the persisted profile value: HKCU\Control Panel\Accessibility\
// StickyKeys "Flags", a REG_SZ decimal such as "510". Returns false on anything
// that is not a plain non-negative decimal that fits in 32 bits.
inline bool parseStickyFlagsString(const wchar_t* s, uint32_t& out)
{
    if (s == nullptr || *s == L'\0')
        return false;
    uint64_t v = 0;
    for (const wchar_t* p = s; *p; ++p)
    {
        if (*p < L'0' || *p > L'9')
            return false;
        v = v * 10 + static_cast<uint64_t>(*p - L'0');
        if (v > 0xFFFFFFFFull)
            return false;
    }
    out = static_cast<uint32_t>(v);
    return true;
}

} // namespace SmoothZoom

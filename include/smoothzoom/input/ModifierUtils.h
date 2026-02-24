#pragma once
// =============================================================================
// SmoothZoom — Modifier Key Utilities
// Helper functions for matching VK codes across L/R variants and converting
// to generic VK codes for GetAsyncKeyState. Used by InputInterceptor.
// =============================================================================

#ifdef _WIN32
#include <windows.h>
#else
// VK constants for non-Windows builds (unit testing on Linux/WSL)
#ifndef VK_SHIFT
#define VK_SHIFT      0x10
#define VK_CONTROL    0x11
#define VK_MENU       0x12
#define VK_LSHIFT     0xA0
#define VK_RSHIFT     0xA1
#define VK_LCONTROL   0xA2
#define VK_RCONTROL   0xA3
#define VK_LMENU      0xA4
#define VK_RMENU      0xA5
#define VK_LWIN       0x5B
#define VK_RWIN       0x5C
#endif
#endif

namespace SmoothZoom
{

// Returns true if vkCode is an L/R variant of the same modifier family as configuredVK.
// E.g., isModifierMatch(VK_RCONTROL, VK_LCONTROL) → true.
inline bool isModifierMatch(int vkCode, int configuredVK)
{
    switch (configuredVK)
    {
    case VK_LCONTROL: case VK_RCONTROL: case VK_CONTROL:
        return vkCode == VK_LCONTROL || vkCode == VK_RCONTROL;
    case VK_LMENU: case VK_RMENU: case VK_MENU:
        return vkCode == VK_LMENU || vkCode == VK_RMENU;
    case VK_LSHIFT: case VK_RSHIFT: case VK_SHIFT:
        return vkCode == VK_LSHIFT || vkCode == VK_RSHIFT;
    case VK_LWIN: case VK_RWIN:
        return vkCode == VK_LWIN || vkCode == VK_RWIN;
    default:
        return vkCode == configuredVK;
    }
}

// Converts a side-specific VK to the generic VK needed by GetAsyncKeyState
// to detect both L and R physical keys. Non-modifier VKs pass through unchanged.
inline int toGenericVK(int vk)
{
    switch (vk)
    {
    case VK_LCONTROL: case VK_RCONTROL: return VK_CONTROL;
    case VK_LMENU:    case VK_RMENU:    return VK_MENU;
    case VK_LSHIFT:   case VK_RSHIFT:   return VK_SHIFT;
    default: return vk;
    }
}

} // namespace SmoothZoom

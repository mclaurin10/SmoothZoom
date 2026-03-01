#pragma once
// =============================================================================
// SmoothZoom — Application-Defined Messages & Command IDs
// Shared between main.cpp and TrayUI.cpp. Single source of truth.
// =============================================================================

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <windows.h>

namespace SmoothZoom
{

// Application-defined window messages
static constexpr UINT WM_OPEN_SETTINGS = WM_APP + 1;
static constexpr UINT WM_TRAYICON      = WM_APP + 2;
static constexpr UINT WM_GRACEFUL_EXIT = WM_APP + 3;

// Context menu command IDs
static constexpr UINT IDM_SETTINGS     = 40001;
static constexpr UINT IDM_TOGGLE_ZOOM  = 40002;
static constexpr UINT IDM_EXIT         = 40003;

} // namespace SmoothZoom

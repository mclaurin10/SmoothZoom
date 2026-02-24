// =============================================================================
// SmoothZoom — MagBridge
// Sole Magnification API wrapper. Doc 3 §3.8
// Only file that #includes <Magnification.h>. (R-01 isolation)
// =============================================================================

#include "smoothzoom/output/MagBridge.h"

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <windows.h>
#include <magnification.h>

#pragma comment(lib, "Magnification.lib")

namespace SmoothZoom
{

// Screen dimensions cached at init — used for input transform rectangles.
// Updated via refreshScreenDimensions() on WM_DISPLAYCHANGE (Phase 6).
static int s_screenWidth = 0;
static int s_screenHeight = 0;

static void cacheScreenDimensions()
{
    s_screenWidth = GetSystemMetrics(SM_CXSCREEN);
    s_screenHeight = GetSystemMetrics(SM_CYSCREEN);
}

bool MagBridge::initialize()
{
    if (initialized_)
        return true;

    cacheScreenDimensions();

    if (!MagInitialize())
        return false;

    // Ensure cursor stays visible while magnified
    MagShowSystemCursor(TRUE);

    initialized_ = true;
    return true;
}

void MagBridge::shutdown()
{
    if (!initialized_)
        return;

    // Shutdown sequence — order matters (see rules/mag-bridge.md):
    // 1. Reset to unmagnified
    MagSetFullscreenTransform(1.0f, 0, 0);

    // 2. Disable input transform
    RECT srcRect = {0, 0, s_screenWidth, s_screenHeight};
    RECT destRect = {0, 0, s_screenWidth, s_screenHeight};
    MagSetInputTransform(FALSE, &srcRect, &destRect);

    // 3. Uninitialize
    MagUninitialize();

    initialized_ = false;
}

bool MagBridge::setTransform(float magnification, float xOffset, float yOffset)
{
    if (!initialized_)
        return false;

    BOOL ok = MagSetFullscreenTransform(magnification,
                                        static_cast<int>(xOffset),
                                        static_cast<int>(yOffset));
    if (!ok)
    {
        lastError_ = GetLastError();
    }

    return ok != FALSE;
}

bool MagBridge::setInputTransform(float magnification, float xOffset, float yOffset)
{
    if (!initialized_)
        return false;

    if (magnification <= 1.0f + 0.001f)
    {
        // At 1.0× or below, disable input transform
        RECT srcRect = {0, 0, s_screenWidth, s_screenHeight};
        RECT destRect = {0, 0, s_screenWidth, s_screenHeight};
        return MagSetInputTransform(FALSE, &srcRect, &destRect) != FALSE;
    }

    // Source rect: the portion of the desktop being displayed (R-04: float division!)
    float viewW = static_cast<float>(s_screenWidth) / magnification;
    float viewH = static_cast<float>(s_screenHeight) / magnification;

    RECT srcRect;
    srcRect.left = static_cast<LONG>(xOffset);
    srcRect.top = static_cast<LONG>(yOffset);
    srcRect.right = static_cast<LONG>(xOffset + viewW);
    srcRect.bottom = static_cast<LONG>(yOffset + viewH);

    // Dest rect: the full screen
    RECT destRect = {0, 0, s_screenWidth, s_screenHeight};

    return MagSetInputTransform(TRUE, &srcRect, &destRect) != FALSE;
}

bool MagBridge::getTransform(float& magnification, float& xOffset, float& yOffset) const
{
    if (!initialized_)
        return false;

    int ix = 0, iy = 0;
    if (!MagGetFullscreenTransform(&magnification, &ix, &iy))
        return false;

    xOffset = static_cast<float>(ix);
    yOffset = static_cast<float>(iy);
    return true;
}

bool MagBridge::setColorInversion(bool /*enabled*/)
{
    // Phase 6 — stub
    return false;
}

// Image smoothing note (AC-2.3.07–AC-2.3.09):
// MagSetFullscreenTransform does not expose a filtering/smoothing parameter.
// MagSetImageScalingCallback is deprecated and only works with windowed
// magnification (not full-screen). The Magnification API always uses bilinear
// filtering (smoothing ON). Nearest-neighbor mode (smoothing OFF) would
// require migration to Desktop Duplication API + Direct3D rendering.
// This is deferred to Phase 5/6 alongside the Desktop Duplication migration (R-01).

} // namespace SmoothZoom

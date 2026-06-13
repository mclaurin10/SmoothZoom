#pragma once
// =============================================================================
// SmoothZoom — ScrollNormalizer
// Device-independent normalization of scroll input into "wheel-equivalent units"
// (the Windows WHEEL_DELTA convention: 120 units = one notch = one ZoomController
// step of kScrollZoomBase).
//
// All scroll producers funnel through here so per-device scaling lives in one
// tested place:
//   - LL mouse hook (WM_MOUSEWHEEL / WM_MOUSEHWHEEL)  — already wheel units (identity)
//   - Raw Input mouse (RAWMOUSE.usButtonData)          — already wheel units (identity)
//   - Precision Touchpad HID two-finger scroll         — device-range normalized
//
// Pure logic — no Win32 API dependencies (CI-safe, unit-tested).
// =============================================================================

#include <cstdint>

namespace SmoothZoom
{

// One standard mouse-wheel notch in wheel-equivalent units (Windows WHEEL_DELTA).
inline constexpr int32_t kWheelDeltaPerNotch = 120;

// ── Mouse wheel / Raw Input mouse ───────────────────────────────────────────
// Mouse wheel deltas already arrive in wheel-equivalent units (HIWORD of
// mouseData / RAWMOUSE.usButtonData). High-resolution wheels report sub-120
// multiples, which are already correctly proportional — so this is identity.
// Provided so every scroll path shares one conceptual entry point (A2).
inline int32_t mouseWheelToWheelEquiv(int32_t rawWheelDelta)
{
    return rawWheelDelta;
}

// ── Precision Touchpad (PTP) two-finger scroll ──────────────────────────────
// PTP HID reports finger Y in device logical units whose range varies widely
// between vendors (Synaptics / Elan / Goodix). Normalizing by the device's own
// logical Y range makes a given fraction-of-pad swipe produce the same zoom on
// any touchpad, independent of the device's raw resolution.

// Y-axis logical extent for a touchpad, captured from its HID report descriptor.
struct PtpAxisScale
{
    int32_t logicalRange = 0;  // logicalMax - logicalMin (device units)
    bool valid() const { return logicalRange > 0; }
};

// Fraction of the touchpad's Y extent that equals one notch of zoom intent.
// ~2% per notch → a full 1x..10x sweep (~24 notches) takes roughly half the pad.
// Tunable; users further adjust feel via the scroll-sensitivity setting (A3).
inline constexpr float kPtpSurfaceFractionPerNotch = 0.02f;

// Fallback device-units-per-notch when the descriptor lacks a usable Y range
// (preserves the historical pre-normalization constant).
inline constexpr float kPtpFallbackUnitsPerNotch = 25.0f;

// Device units of finger Y travel that equal one notch, for a given device.
inline float ptpUnitsPerNotch(const PtpAxisScale& scale)
{
    if (scale.valid())
    {
        float u = static_cast<float>(scale.logicalRange) * kPtpSurfaceFractionPerNotch;
        if (u >= 1.0f)
            return u;
    }
    return kPtpFallbackUnitsPerNotch;
}

// Convert an averaged PTP finger Y delta (device units) to wheel-equivalent
// units (120 per notch), device-range-normalized. Sub-notch precision is
// preserved (returns float; callers accumulate a fractional remainder so
// continuous touchpad input yields continuous zoom rather than 10%-quantized
// steps). Sign is preserved.
inline float ptpDeltaToWheelEquiv(float deviceUnitsDeltaY, const PtpAxisScale& scale)
{
    float unitsPerNotch = ptpUnitsPerNotch(scale);
    return deviceUnitsDeltaY * static_cast<float>(kWheelDeltaPerNotch) / unitsPerNotch;
}

} // namespace SmoothZoom

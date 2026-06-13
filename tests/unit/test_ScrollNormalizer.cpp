// =============================================================================
// Unit tests for ScrollNormalizer — device-independent scroll normalization.
// Pure logic — no Win32 API dependencies.
// =============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "smoothzoom/input/ScrollNormalizer.h"

using namespace SmoothZoom;
using Catch::Approx;

// ── Mouse wheel: identity (already wheel-equivalent units) ───────────────────

TEST_CASE("mouseWheelToWheelEquiv is identity", "[ScrollNormalizer]")
{
    REQUIRE(mouseWheelToWheelEquiv(120) == 120);
    REQUIRE(mouseWheelToWheelEquiv(-120) == -120);
    REQUIRE(mouseWheelToWheelEquiv(0) == 0);
    REQUIRE(mouseWheelToWheelEquiv(40) == 40);    // high-resolution sub-notch
    REQUIRE(mouseWheelToWheelEquiv(-7) == -7);
}

// ── PTP units-per-notch derivation ──────────────────────────────────────────

TEST_CASE("ptpUnitsPerNotch uses device range when valid", "[ScrollNormalizer]")
{
    PtpAxisScale s{2600};
    REQUIRE(ptpUnitsPerNotch(s) == Approx(2600 * kPtpSurfaceFractionPerNotch));
}

TEST_CASE("ptpUnitsPerNotch falls back when range is invalid", "[ScrollNormalizer]")
{
    REQUIRE(ptpUnitsPerNotch(PtpAxisScale{0}) == Approx(kPtpFallbackUnitsPerNotch));
    REQUIRE(ptpUnitsPerNotch(PtpAxisScale{-5}) == Approx(kPtpFallbackUnitsPerNotch));
}

TEST_CASE("ptpUnitsPerNotch falls back when derived units < 1", "[ScrollNormalizer]")
{
    // A tiny logical range would yield sub-1 units/notch (absurdly sensitive);
    // fall back to keep behaviour sane.
    PtpAxisScale tiny{10}; // 10 * 0.02 = 0.2 < 1
    REQUIRE(ptpUnitsPerNotch(tiny) == Approx(kPtpFallbackUnitsPerNotch));
}

// ── PTP delta → wheel-equivalent, device-independent ─────────────────────────

TEST_CASE("ptpDeltaToWheelEquiv: same swipe fraction → same zoom on any device", "[ScrollNormalizer]")
{
    // Two touchpads with very different raw resolutions. A swipe spanning the
    // SAME fraction of the pad (here, exactly one notch worth) must produce the
    // same wheel-equivalent regardless of device resolution.
    PtpAxisScale lowRes{2600};
    PtpAxisScale highRes{5200};

    float swipeLow  = ptpUnitsPerNotch(lowRes);   // one notch of travel on low-res pad
    float swipeHigh = ptpUnitsPerNotch(highRes);  // one notch of travel on high-res pad

    REQUIRE(ptpDeltaToWheelEquiv(swipeLow, lowRes)   == Approx(kWheelDeltaPerNotch));
    REQUIRE(ptpDeltaToWheelEquiv(swipeHigh, highRes) == Approx(kWheelDeltaPerNotch));
}

TEST_CASE("ptpDeltaToWheelEquiv preserves sign", "[ScrollNormalizer]")
{
    PtpAxisScale s{2600};
    float up = ptpDeltaToWheelEquiv(ptpUnitsPerNotch(s), s);
    float down = ptpDeltaToWheelEquiv(-ptpUnitsPerNotch(s), s);
    REQUIRE(up > 0.0f);
    REQUIRE(down < 0.0f);
    REQUIRE(up == Approx(-down));
}

TEST_CASE("ptpDeltaToWheelEquiv: sub-notch input yields sub-notch output", "[ScrollNormalizer]")
{
    PtpAxisScale s{2600};
    float quarterNotch = ptpUnitsPerNotch(s) * 0.25f;
    REQUIRE(ptpDeltaToWheelEquiv(quarterNotch, s) == Approx(kWheelDeltaPerNotch * 0.25f));
}

TEST_CASE("ptpDeltaToWheelEquiv: fallback path matches historical 25-units-per-notch", "[ScrollNormalizer]")
{
    // With no device range, 25 device units == one notch == 120 wheel-equiv.
    REQUIRE(ptpDeltaToWheelEquiv(kPtpFallbackUnitsPerNotch, PtpAxisScale{0})
            == Approx(kWheelDeltaPerNotch));
}

TEST_CASE("ptpDeltaToWheelEquiv: zero delta is zero", "[ScrollNormalizer]")
{
    REQUIRE(ptpDeltaToWheelEquiv(0.0f, PtpAxisScale{2600}) == Approx(0.0f));
}

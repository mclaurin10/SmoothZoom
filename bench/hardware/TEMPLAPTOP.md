# Hardware profile — TEMPLAPTOP (conference laptop)

Captured 2026-10-01 with WMI/CIM, `dxdiag /t`, `DwmGetCompositionTimingInfo`, `powercfg` and
`PowerGetEffectiveOverlayScheme`, all from a non-elevated session. Re-capture if the panel,
driver or power source changes, because every bench number below depends on them.

| Item | Value |
|---|---|
| Model | HP EliteBook X G1i 14" Notebook Next Gen AI PC |
| CPU | Intel Core Ultra 7 258V (Lunar Lake), 8C/8T |
| RAM | 32 GB (on-package) |
| OS | Windows 11 Pro 25H2, build **26200.9457** |
| User | `TEMPLAPTOP\duncan`. Split-token local admin (Administrators *deny-only*, medium IL), so UAC elevates the **same** account. The EliteBook-1040 separate-admin-account trap (`hardware-accommodation-handoff.md` §6.5) does **not** apply. |

## Display

| Item | Value |
|---|---|
| Panel | Internal eDP, AU Optronics `AUOC9B9`, 30 × 19 cm |
| Resolution | **1920 × 1200** physical, single monitor, virtual desktop = primary (origin 0,0) |
| DPI scale | **150 %** (system DPI 144). SmoothZoom is PerMonitorV2, so all bench coordinates are physical px. |
| Refresh | **60 Hz** nominal. DWM reports `rateRefresh = 10000000/166974` = **59.890 Hz**, a period of **16.697 ms**. QPC frequency is 10 MHz. |
| VRR | Panel EDID range **40–60 Hz** (`Win32_VideoController` Min/MaxRefreshRate). Windows' VRR toggle is not set explicitly (`DirectXUserGlobalSettings` absent, so the OS default applies). VRR only affects game swap-chains: **DWM desktop composition, and so `DwmFlush` and `MagSetFullscreenTransform`, runs at the fixed 59.89 Hz.** Dynamic Refresh Rate does not apply (max 60 Hz). Panel Self Refresh may idle the link when nothing changes; this is invisible to DWM timing. |
| HDR / color | HDR not supported; Advanced Color supported (SDR). |
| MPO | 3 planes (MPO3), with stretch and bilinear capability. |

## GPU

| Item | Value |
|---|---|
| GPU | **Intel Arc 140V** (Xe2, integrated), driver **32.0.101.6913**, WDDM 3.2 |
| Hybrid | **No.** Single integrated GPU. dxdiag reports `Microsoft Graphics Hybrid: Not Supported`. The second adapter it lists (MCDM 3.2, 0 MB) is the NPU, not a GPU. |
| Comparison | This is a much stronger iGPU than the Doc 1 §6.4 / Doc 7 §5 reference (Intel UHD 620). Results here are *not* the reference-hardware audit. |

## Input devices

| Item | Value |
|---|---|
| Touchpad | **ELAN0124**, I2C HID, exposes a `HID-compliant touch pad` collection (`HID\ELAN0124&COL03`). It is a **Windows Precision Touchpad** (PTP settings present under `HKCU\…\PrecisionTouchPad`). |
| PTP settings | **ScrollDirection = 0 → natural (reverse) scrolling ON**, PanEnabled = 1, ZoomEnabled = 1 (pinch), CursorSpeed = 10 |
| Touchscreen | ELAN2513 I2C (`HID-compliant touch screen`). Not used by SmoothZoom (Doc 1 §3.7 out of scope). |
| Mouse | **Logitech, via a Logi Bolt receiver** (`HID\VID_046D&PID_C548`), attached after the first capture. Its wheel arrives as **detented ±120** steps; the model is not identifiable from PnP. |

## Power

| Item | Value |
|---|---|
| Plan | `HP Optimized (Modern Standby)` (`fb5220ff-7e1a-47aa-9a42-50ffbf45c673`) |
| Power-mode overlay | Balanced (effective overlay GUID `00000000-…`, the default/"Balanced" slider) |
| Source at capture | **Battery** (BatteryStatus = 1, discharging, 90 %). Power source changes CPU/GPU clocks and possibly DWM scheduling, so the bench records it in every run. **Baselines on battery and on AC are not comparable.** |

## Wheel/touchpad arrival at the LL hook (observed 2026-10-01)

Captured with `tools/observe-wheel.ps1` (observe-only `WH_MOUSE_LL`). Raw data: `wheel-observe.csv`, 834 events.

**Answer: everything arrives as `WM_MOUSEWHEEL`. Nothing was converted to `WM_MOUSEHWHEEL`, with or without Shift, from the touchpad or the mouse.** Shift is visible to the hook (`GetAsyncKeyState`) on every event of a Shift gesture. So with Shift as the modifier, today's code takes the `WM_MOUSEWHEEL` branch (`InputInterceptor.cpp:125`), and the `WM_MOUSEHWHEEL` negation path (`:163-208`) is never exercised on this machine.

| Gesture | Foreground | Arrives as | Δ per event | Event spacing | Notes |
|---|---|---|---|---|---|
| two-finger scroll | Windows Terminal | WHEEL | 2–323 (median 33); only 28/165 are multiples of 120 | median **7.1 ms** (~140 Hz) | Each burst ends in an OS-generated **inertia tail** that decays to single digits (e.g. `-67 -62 -50 … -12 -8`) over ~100–300 ms |
| Shift + two-finger | Windows Terminal | **WHEEL** (shift=1) | 2–311 (median 20) | median 6.9 ms | Same continuous/inertia shape |
| Win + two-finger | Windows Terminal | WHEEL (win=1) | 2–**1014** (median 35) | median 6.7 ms | Fast flicks produce a single very large first event (−1014, 555), then a long inertia tail |
| mouse wheel (Logitech) | Windows Terminal | WHEEL | exactly ±120 | 15–40 ms within a spin | Detented |
| Shift + mouse wheel | Windows Terminal | **WHEEL** (shift=1) | exactly ±120 | 7–30 ms | Not converted by the driver/OS |
| any gesture | Notepad / Edge | — | — | — | **No events were recorded with Notepad or Edge in front.** Either the Edge/Notepad passes weren't done, or Edge's Direct Manipulation swallowed the touchpad gesture before the hook. That is the expected DManip behaviour (`hardware-accommodation-handoff.md` §3). Confirm with `--record` in session 2, which also logs the PTP HID path. |

Implications for the bench:
- The touchpad cadence (~140 Hz) is **2.3 events per 59.89 Hz frame**, uneven (2 or 3), so the per-frame accumulated delta alternates. The synthetic touchpad trace reproduces this.
- The single huge first event (up to ~8.5 notches) at flick start is a real input shape. The free-spin and touchpad traces include large first events.

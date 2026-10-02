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
| Mouse | **No external mouse attached.** The only HID mouse collection is the touchpad's (`ELAN0124&COL01`). A *detented* wheel can therefore only be exercised with synthetic traces on this machine until a mouse is plugged in. |

## Power

| Item | Value |
|---|---|
| Plan | `HP Optimized (Modern Standby)` (`fb5220ff-7e1a-47aa-9a42-50ffbf45c673`) |
| Power-mode overlay | Balanced (effective overlay GUID `00000000-…`, the default/"Balanced" slider) |
| Source at capture | **Battery** (BatteryStatus = 1, discharging, 90 %). Power source changes CPU/GPU clocks and possibly DWM scheduling, so the bench records it in every run. **Baselines on battery and on AC are not comparable.** |

## Wheel/touchpad arrival at the LL hook — **PENDING user gestures**

Question: does Shift + wheel or two-finger scroll arrive as `WM_MOUSEWHEEL`, or is it converted to
`WM_MOUSEHWHEEL` before the hook sees it? This needs physical gestures. Run
`tools/observe-wheel.ps1` (observe-only, no elevation) and perform the listed gestures. The
results go here.

| Gesture | Foreground | Arrives as | Δ per event | Notes |
|---|---|---|---|---|
| two-finger scroll | Notepad | _pending_ | | |
| Shift + two-finger | Notepad | _pending_ | | |
| two-finger scroll | Edge (DManip) | _pending_ | | expect nothing at the hook (PTP HID path only) |
| Shift + two-finger | Edge (DManip) | _pending_ | | |

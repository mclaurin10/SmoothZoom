# SmoothZoom smoothness tuning: final report (TEMPLAPTOP, AC, 2026-10-01/02)

Branch `perf/laptop-tuning` (from `feature/shift-scroll`), not pushed. Yardstick frozen at tag `loop-start` (c512a91).
Iteration log: `bench/LOOP.md`. Hook gate log: `bench/loop-gate.log`.
Gate outputs: `bench/baseline/TEMPLAPTOP-AC/{gate1-preboot,final-gate-HEAD}.json`.

## Result
| Gate | Code | Failing | Environment |
|---|---|---|---|
| Baseline (yardstick v2, before tuning) | e90ca18 | 49 / 160 (C-21–23 not yet defined) | clean |
| Gate 1 (before the review fixes) | f14bc09 | 7 / 172 | clean, pre-reboot |
| **Final (HEAD)** | f83c248 + docs | **12 / 172** | clean: AC, display never sleeps, only Claude open, timing back to pre-reboot levels (wake after vblank 1.2–1.6 ms), no stalls |

The 5 extra failures between gate 1 and final are all C-18 (snap-backs). The independent review showed the gate-1 passes were partly bought with screen-edge judder and hidden touchpad lag. The honest version keeps the anchor fix within visible-error bounds and leaves touchpad input truly direct.

## Before / after (median of 5 runs per trace)
**C-05 missed %** (median of runs)

| Trace | Baseline (v2, pre-tuning) | Gate 1 (pre-reboot) | Final (HEAD) |
|---|---|---|---|
| detented-sweep | 46.3 | 0.0 | 0.0 |
| freespin-burst | 38.9 | 0.0 | 0.0 |
| wheel-sweep | 8.1 | 0.0 | 0.0 |
| wheel-flick | 55.4 | 0.0 | 0.0 |
| touchpad-edge | 0.5 | 0.5 | 0.5 |
| touchpad-flick | 10.1 | 6.7 | 10.1 |
| touchpad-terminal | 2.3 | 2.4 | 2.8 |
| touchpad-ptp | 0.0 | 0.0 | 0.0 |

**C-08 max gap (P)** (median of runs)

| Trace | Baseline (v2, pre-tuning) | Gate 1 (pre-reboot) | Final (HEAD) |
|---|---|---|---|
| detented-sweep | 2.94 | 1.14 | 1.13 |
| freespin-burst | 3.06 | 1.14 | 1.11 |
| wheel-sweep | 3.02 | 1.16 | 1.14 |
| wheel-flick | 6.02 | 1.13 | 1.11 |
| touchpad-edge | 2.03 | 2.05 | 2.01 |
| touchpad-flick | 4.93 | 4.90 | 4.91 |
| touchpad-terminal | 3.93 | 4.00 | 3.96 |
| touchpad-ptp | 1.12 | 1.13 | 1.11 |

**C-11 cold start ms** (median of runs)

| Trace | Baseline (v2, pre-tuning) | Gate 1 (pre-reboot) | Final (HEAD) |
|---|---|---|---|
| detented-sweep | 160.5 | 0.1 | 0.1 |
| freespin-burst | 160.3 | 0.1 | 0.1 |
| wheel-sweep | 159.8 | 0.0 | 0.1 |
| wheel-flick | 159.0 | 0.1 | 0.1 |
| touchpad-edge | 159.8 | 0.1 | 0.1 |
| touchpad-flick | 160.0 | 0.1 | 0.1 |
| touchpad-terminal | 160.0 | 0.1 | 0.1 |
| touchpad-ptp | 160.7 | 0.1 | 0.1 |

**C-12 in→upd p95 ms** (median of runs)

| Trace | Baseline (v2, pre-tuning) | Gate 1 (pre-reboot) | Final (HEAD) |
|---|---|---|---|
| detented-sweep | 15.2 | 15.7 | 15.5 |
| freespin-burst | 15.2 | 15.8 | 15.9 |
| wheel-sweep | 15.3 | 15.2 | 15.5 |
| wheel-flick | 15.6 | 15.8 | 16.0 |
| touchpad-edge | 15.9 | 15.7 | 16.0 |
| touchpad-flick | 15.8 | 15.8 | 15.9 |
| touchpad-terminal | 15.8 | 15.8 | 15.8 |
| touchpad-ptp | 15.8 | 15.6 | 15.9 |

**C-14 settle ms** (median of runs)

| Trace | Baseline (v2, pre-tuning) | Gate 1 (pre-reboot) | Final (HEAD) |
|---|---|---|---|
| detented-sweep | 5 | 86 | 88 |
| freespin-burst | 15 | 94 | 92 |
| wheel-sweep | 17 | 97 | 97 |
| wheel-flick | 16 | 97 | 97 |
| touchpad-edge | 17 | 46 | 17 |
| touchpad-flick | 16 | 48 | 17 |
| touchpad-terminal | 16 | 50 | 16 |
| touchpad-ptp | 15 | 30 | 16 |

**C-15 anchor p95** (median of runs)

| Trace | Baseline (v2, pre-tuning) | Gate 1 (pre-reboot) | Final (HEAD) |
|---|---|---|---|
| detented-sweep | 1.83 | 0.96 | 0.98 |
| freespin-burst | 1.76 | 0.98 | 0.98 |
| wheel-sweep | 1.93 | 0.97 | 0.98 |
| wheel-flick | 1.93 | 0.97 | 0.98 |
| touchpad-edge | 1.96 | 0.97 | 0.97 |
| touchpad-flick | 1.95 | 0.97 | 0.98 |
| touchpad-terminal | 1.95 | 0.98 | 0.98 |
| touchpad-ptp | 1.96 | 0.98 | 0.96 |

**C-18 snap-back %** (median of runs)

| Trace | Baseline (v2, pre-tuning) | Gate 1 (pre-reboot) | Final (HEAD) |
|---|---|---|---|
| detented-sweep | 44 | 14 | 27 |
| freespin-burst | 22 | 2 | 8 |
| wheel-sweep | 0 | 10 | 15 |
| wheel-flick | 30 | 11 | 20 |
| touchpad-edge | 16 | 1 | 17 |
| touchpad-flick | 46 | 15 | 44 |
| touchpad-terminal | 50 | 24 | 49 |
| touchpad-ptp | 45 | 10 | 46 |

**C-20 half-resp ms** (median of runs)

| Trace | Baseline (v2, pre-tuning) | Gate 1 (pre-reboot) | Final (HEAD) |
|---|---|---|---|
| detented-sweep | 5 | 23 | 25 |
| freespin-burst | 14 | 27 | 30 |
| wheel-sweep | 15 | 33 | 32 |
| wheel-flick | 16 | 33 | 32 |
| touchpad-edge | 11 | 12 | 11 |
| touchpad-flick | 12 | 13 | 12 |
| touchpad-terminal | 12 | 11 | 12 |
| touchpad-ptp | 4 | 6 | 5 |

Notes: baseline wheel-sweep C-18 = 0 is an artifact. Each slow notch landed as one step, so there were no frame pairs to score. Settle and half-response rise on wheel traces by design: a notch now glides over ≤ 97 ms instead of landing in one frame. Both stay within C-14 (≤ 100 ms) and C-20 (≤ 40 ms).

## What each iteration did
1. **Cold start (R-24):** `MagBridge::warmUp()` pays DWM's one-time magnifier setup (~170 ms) at launch. First zoom stall 160 ms → 0.1 ms.
2. **Wheel stepping (AC-2.2.03):** whole-notch input is smoothed toward the accumulated target in log space (critically damped, ω = 100/s). It rests ≤ 97 ms after the input, timed from a QPC stamp written by the hook. Touchpad stays direct. Missed refreshes on wheel traces 8–55 % → 0.
3. **Anchor (R-23):**
   - Offsets are rounded, not truncated. Anchor error relative to the pixel floor 1.8–2.0 → 0.96–0.98.
   - A small monotone zoom nudge during wheel smoothing reduces snap-backs. After review it is capped so no screen pixel moves more than the anchor error it saves, and it is never applied to touchpad input or keyboard animations.
   - Two looser touchpad variants were rejected: one broke C-23, the other didn't help.
4. **Focus vs scroll (AC-2.1.09/AC-2.5.10, user-approved):** a scroll zoom counts as pointer activity, so it zooms at the pointer even after keyboard focus moved (e.g. after clicking into a text field). This was a real bug, found because every post-reboot run followed the Claude prompt field.
5. **Review fixes:** edge-movement cap, no touchpad nudge, counter saturation, comment and AC wording.

## Still failing, and why
- **Touchpad holes: C-05/C-08 on tp-flick, tp-terminal, tp-edge; C-07 on tp-flick.** The recorded input itself pauses 39–80 ms mid-gesture (inertia tails, finger pauses). A direct touchpad display correctly doesn't move then; the zoom change across these gaps is mostly < 0.1 %. Passing would require smoothing touchpad input, which the brief ruled out. The criterion counts sub-visible pauses as holes; a motion-size threshold for touchpad would be a reasonable revision.
- **Snap-backs: C-18 on 6 traces** (detented 27 %, wheel-flick 20 %, tp-edge 17 %, tp-flick 44 %, tp-terminal 49 %, ptp 46 %). With integer viewport offsets, the anchor error sits within ±0.5 px of the pixel grid and flips back and forth as the zoom moves. Removing it without moving the jitter to the screen edges, or adding touchpad lag, isn't possible with the Magnification API. Wheel traces are still below rounding-only (10–40 %); touchpad is at rounding level. The 15 % threshold was calibrated on an uncapped simulation and is not achievable under the honest bounds.

## Environment findings (R-32)
- An unplanned reboot (00:40) left the render loop waking 2.4–3.4 ms after vblank (normally 1.2–2.1) and `SetCursorPos` occasionally slow (1–5 ms). That made the same code fail 14–16 checks. It recovered after a clean reboot with only Claude open.
- The AC display timeout had become 20 min, which stopped one final gate.
- Gates 2–8 were invalid (battery, live touchpad input, timing, focus). The gate's environment check and stall detector caught what they were built for.

## Check by hand
1. **Wheel (Logitech, detented):** notches glide, don't lag, rest within ~0.1 s. Fast spins feel continuous.
2. **Touchpad:** Shift + two-finger should feel exactly as direct as before.
3. **Cursor alignment:** clicks land on targets at 5× and 10× across the screen, including near the edges.
4. **Shift + two-finger in Edge:** zooms, and Edge also scrolls sideways (known, R-30, not changed).
5. **Click into a text field, then Shift+scroll without moving the mouse:** zooms at the pointer.
6. **Launch at 1.0×:** no visible flicker or blur from the warm-up. Optionally check a full-screen app with PresentMon for lost independent flip.
7. **After sleep/resume:** is the first zoom smooth? The warm-up runs once per launch.

## Install this build for the conference (release, no dev tools)
1. Quit SmoothZoom if running (tray → Exit). Only one instance can run.
2. Build Release, non-elevated, from this branch:
   `powershell -NoProfile -Command ". .\tools\_dev-common.ps1; $tc=Get-SzToolchain; & $tc.CMake -S . -B build -G $tc.Generator -A x64; & $tc.CMake --build build --config Release --parallel"`
3. Sign and install, **elevated**: `powershell -NoProfile -ExecutionPolicy Bypass -File scripts\deploy-machinestore.ps1 -Config Release`
   This creates or uses the LocalMachine `CN=SmoothZoom Dev` cert and installs to `C:\Program Files\SmoothZoom\`.
   Don't use `scripts\deploy.ps1`: it pins the VS 2022 generator, which this laptop doesn't have.
4. Launch `C:\Program Files\SmoothZoom\SmoothZoom.exe`. Your existing `%APPDATA%\SmoothZoom\config.json` keeps its saved modifier and settings.

## Revert the dev setup afterward
- Elevated: `tools\dev-revert.ps1` removes the "SmoothZoom Bench Dev (TEMPLAPTOP)" cert (expires 2027-04-01) and `C:\Program Files\SmoothZoom Dev\`.
- Already done: Stop hook removed from `.claude/settings.local.json`; `bench/.loop-active` deleted.
- Optional: restore your display timeout preference.

# Baseline — TEMPLAPTOP on AC (session 1, no tuning)

| | |
|---|---|
| Captured | 2026-10-01 17:53 local |
| App code | `4ab91ca`. The aggregate reports `+dirty` only because bench/ files were not yet committed; no app source changed. |
| Build | `build-dev` Release, `SMOOTHZOOM_DEV_TOOLS=ON` (telemetry adds two QPC reads and one `DwmGetCompositionTimingInfo` per frame) |
| Power | **AC** (PowerLineStatus Online), Balanced overlay. DWM period **16.667 ms (60.00 Hz)**. On battery it was 16.697 ms (59.89 Hz). |
| Settings | Built-in defaults (`--bench` does not read `config.json`): Win modifier, sensitivity 1.0, min 1×, max 10×. |
| Method | `tools/bench/run-bench.ps1 -All -Runs 3`. Each run is a fresh process with a warm-up (one notch in, Win+Esc back to 1.0×) before telemetry. Cursor pinned at (710, 444) = 37%/37% of 1920×1200. |
| Raw data | `<trace>/run<N>/{summary.json, frames.csv, events.csv}`, `aggregate.json`. Regenerate the table with `python tools/bench/report.py bench/baseline/TEMPLAPTOP-AC/aggregate.json`. |

Each cell is the median of 3 runs, with (min–max) when the runs differ. Run-to-run spread is small because
the replay is deterministic and the machine was otherwise idle.

| Metric | detented-sweep | freespin-burst | touchpad-ptp |
|---|---|---|---|
| refresh period (ms) | 16.667 | 16.667 | 16.667 |
| transform updates | 58 | 22 (22–23) | 106 |
| update interval p50 (periods) | 2.00 (1.99–2.00) | 1.52 (1.04–1.53) | 1.00 |
| update interval p95 (periods) | 2.94 (2.88–2.96) | 3.01 (3.00–3.01) | 1.06 (1.05–1.06) |
| update interval p99 (periods) | 2.97 (2.96–2.99) | 3.02 (3.01–3.02) | 1.07 |
| update interval max (periods) | 2.99 (2.96–2.99) | 3.03 (3.02–3.03) | 1.12 (1.09–1.14) |
| update interval p50 / max (ms) | 33.3 (33.2–33.3) / 49.9 (49.4–49.9) | 25.3 (17.3–25.4) / 50.5 (50.3–50.5) | 16.7 / 18.6 (18.2–19.0) |
| refreshes during gesture | 108 | 36 | 106 |
| refreshes missed during gesture | 50 | 14 (13–14) | 0 |
| missed (%) | 46.3 | 38.9 (36.1–38.9) | 0.0 |
| refreshes with >1 update | 0 | 0 | 0 |
| render tick interval p95 / max (ms) | 17.6 (17.5–17.6) / 18.5 (18.3–18.7) | 17.4 (17.2–17.4) / 18.6 (18.6–19.0) | 17.5 (17.4–17.6) / 18.6 (18.2–18.9) |
| late ticks (>1.5 periods) | 0 | 0 | 0 |
| update phase after vblank p50 (ms) | 1.1 (1.1–1.2) | 0.9 (0.9–1.0) | 1.2 (1.2–1.6) |
| MagSetFullscreenTransform call max (ms) | 0.028 (0.023–0.074) | 0.024 (0.022–0.037) | 0.068 (0.044–0.083) |
| cold-start first transform (ms) | 168 (167–169) | 165 (162–169) | 168 (166–168) |
| anchor error p95 / max (phys px) | 9.49 / 11.45 | 2.80 / 2.94 | 6.07 (6.05–6.15) / 8.03 |
| &nbsp;&nbsp;of which offset truncation p95 | 9.49 | 2.80 | 6.07 (6.05–6.15) |
| round-trip error (%) | 0.00000 | 0.00002 | 0.00001 (0.00001–0.00002) |
| input → transform update p50 / p95 (ms) | 7.6 (7.5–8.1) / 15.5 (15.3–15.7) | 7.3 (6.9–7.6) / 15.8 (15.0–16.2) | 8.5 (8.5–8.8) / 16.2 (15.9–16.2) |
| input → next vblank p50 / p95 (ms) | 23.0 (22.8–23.7) / 31.5 (30.6–31.7) | 23.2 (22.6–23.5) / 31.8 (31.1–32.0) | 23.8 (23.6–23.9) / 31.2 (31.1–31.3) |
| settle after last input max (ms) | 4.1 (3.5–4.7) | 15.1 (14.9–15.8) | 15.4 (14.3–16.2) |
| per-refresh zoom step p50 / max (ln) | 0.095 / 0.095 | 0.095 / 0.477 (0.477–0.572) | 0.022 (0.021–0.022) / 0.120 (0.118–0.120) |
| zoom max / end | 9.99 / 1.00 | 5.56 / 1.01 | 6.73 / 1.05 |

## Against `bench/criteria.json`

The gate reproduced these numbers (`gate.ps1`, 3 runs) and failed **15 of 51** checks, which fall into
four families:

| Family | Criteria | Fails on | Baseline |
|---|---|---|---|
| **Stepped motion** (direct application of each notch) | C-05, C-07, C-08 | detented, free-spin | 46% / 39% of refreshes during a gesture get no update; updates 2–3 periods apart; free-spin applies up to 5 notches (+61%) in a single frame |
| **Anchor shimmer** (offset truncation `MagBridge.cpp:87-89`) | C-15, C-16 | all three | the point under the pointer moves 2.8–9.5 px (p95), up to 11.4 px; it scales with zoom |
| **Cold-start stall** (first transform after `MagInitialize`) | C-11 | all three | 162–169 ms blocking `MagSetFullscreenTransform` call, once per process |

Passing: pacing (no doubled refreshes, no late ticks, 1.00 period per update on the touchpad stream),
latency (input→update p95 ≤ 16.2 ms), settle, round-trip drift (≤ 2e-5 %) and set-call cost (≤ 0.08 ms).

## Observations not captured by the 3-run table

1. **Whole-process stall in 2 of 24 runs (both in one gate invocation).** In a later `-NoBuild` gate run the main thread (bench injector),
   the render thread and DWM's `cRefresh` counter all paused about 940 ms (freespin, at 1.21×) and
   246 ms (touchpad, during a pause at 3.29×). The queued wheel input then landed as one +1920 jump.
   The latency metric did not catch it, because injection was delayed too. This is environmental or
   intermittent and not yet explained. Hypothesis: it coincides with the zoom first exceeding 1.005×,
   when the main thread's tray timer calls the synchronous `Shell_NotifyIcon`. Low-level hooks also run
   on the main thread (R-05), so real input would be delayed the same way. Results are under
   `bench/results/` (not committed), and session 2 should reproduce it.
2. **`DwmFlush` blocks while DWM is idle.** During the same stall `cRefresh` did not advance, so
   `DwmFlush` only returns when DWM composes. With a static screen and a static zoom, the render loop
   can therefore sleep through incoming input. It is usually masked by something else animating on
   screen.
3. **Soft floor leaves zoom above 1×.** The touchpad trace ends at 1.05× after a gesture whose
   magnitude would reach 1.0× without the floor attenuation (AC-2.1.13/15 interplay). The free-spin
   trace ends at 1.01×.
4. **Refresh rate depends on the power source:** 60.00 Hz on AC, 59.89 Hz on battery.

## Session 2 re-check: Shift traces (2026-10-01, branch `feature/shift-scroll`)

The synthetic traces now hold **Shift** (`modifier_vk 0xA0`, `shift=1, win=0` on every wheel row). Timing and deltas are byte-for-byte unchanged (`tools/bench/gen_synthetic.py`). The question was whether making Shift the default modifier, and the hook changes behind it, changed smoothness. **It did not.**

| | |
|---|---|
| Build | `7e96646` (`build-dev`, Release, `SMOOTHZOOM_DEV_TOOLS=ON`) |
| Power | AC, 16.667 ms (60.00 Hz) |
| Method | `run-bench.ps1 -All -Runs 3`, three sets. `shift-scroll-1` and `shift-scroll-2` used the Shift traces. `shift-scroll-ab-win` replayed the **session-1 Win traces on the new build**, an A/B on the modifier with the build held fixed. Raw data is in `bench/results/` (gitignored). |

| Check | Baseline (Win) | Shift set 1 | Win traces, new build | Shift set 2 |
|---|---|---|---|---|
| Gate (`criteria.json`) | 15 / 51 fail | 15 / 51, same checks | 15 / 51, same checks | 15 / 51, same checks |
| Updates (detented / free-spin / touchpad) | 58 / 22–23 / 106 | 58 / 22 / 105–107 | 58 / 22 / 105–106 | 58 / 22 / 106 |
| Missed refreshes during gesture | 46.3 % / 38.9 % / 0 % | same | same | same |
| Wheel events consumed | all | 60 / 38 / 245, all | all | all |
| Late ticks; max render tick | 0; 19.0 ms | 0; 19.4 ms | 0; 19.5 ms | 0; 19.5 ms |

Two cells moved in set 1, and the A/B shows both are noise, not the modifier:

- **Detented update-interval p95**: 2.25 (set 1) vs 2.94 baseline. The distribution is bimodal (2 or 3 periods per notch, depending on where the 26–38 ms gaps fall against vblank), and one interval more or less flips p95. The Win traces on the new build gave 2.93, 2.96 and **2.27** in three runs. Free-spin p50 flips between about 1.07 and 1.52 in the same way, in every set.
- **`MagSetFullscreenTransform` max**: 0.05–0.31 ms (one run 1.0 ms), against 0.02–0.08 ms in the baseline. This was equally high with the Win traces, so it is the machine's state that day, not Shift. It is far under the C-10 ceiling (2 ms).

**No R-25 stall** in any of the 27 runs (no late ticks, no gap in `cRefresh`), so nothing was rerun or set aside.

From this commit on, `run-bench.ps1` passes `--config <run dir>\config.json` (a file that does not exist, so built-in defaults load as before). A bench never reads or writes `%APPDATA%\SmoothZoom\` again. The runs above predate that flag; `--bench` did not load or save `config.json` then either.

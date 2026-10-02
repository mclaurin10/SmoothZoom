# Tuning loop log — TEMPLAPTOP (AC), session 3

Branch `perf/laptop-tuning` (from `feature/shift-scroll`). Yardstick frozen at tag `loop-start`:
`bench/criteria.json`, `tools/bench/`, `bench/traces/`, `src/app/DevTools.{cpp,h}`,
`include/smoothzoom/common/DevTelemetry.h`. Every run uses `--config` (never the real config).

Per iteration: hypothesis → change → `tools\bench\gate.ps1 -Quick` → commit with before/after →
keep or revert. Full gate (5 runs/trace) runs on every stop attempt (Stop hook, `stop-gate.ps1`).

| # | Item | Change | Quick gate: fails before → after | Key numbers | Decision |
|---|------|--------|----------------------------------|-------------|----------|
| 1 | Cold start (R-24) | `MagBridge::warmUp()` at render-thread init (after `initComplete_`, skipped on magnifier conflict): `MagSetFullscreenTransform(1.0001)` then `1.0` — pays DWM's one-time magnifier setup (165–188 ms, logged) at launch instead of on the first zoom | 49 → 42 | C-11 cold start 159–161 ms → 0.05–0.16 ms on all 8 traces; C-12 unchanged (15.1–16.0 ms). One late tick (26 ms, mid-trace, 1 run) on touchpad-edge = scheduler noise | **Keep** |
| 2 | Wheel stepping (AC-2.2.03) | ZoomController: target = accumulated model; whole-notch input (multiple of 120, no sub-notch delta in the last 250 ms) smoothed toward it in log space, critically damped (ω = 100/s, exact step for any dt); touchpad/sub-notch input stays direct. Scroll takes over from the displayed level (AC-2.2.08); toggles/settings capture the scroll target; `endScroll` waits for rest. First cut timed the tail from the consuming frame (effectively 67 ms) → 2-refresh holes before notches 90–97 ms apart, and notches pushing into 10× restarted it (99 ms settle). Fix: hook stamps QPC of each wheel input (`lastScrollInputQpc`); the tail ends on the last frame still ≤ 97 ms after the newest notch that moved the target | 42 → 30 → 29 | Wheel traces: C-05 missed 36–55 % → 0; C-07 p95 2.97–5.06 → 1.05–1.07 P; C-08 max 3.03–6.01 → 1.09–1.16 P; C-14 settle 84–97 ms (≤ 100); C-20 half-response 23–32 ms (≤ 40); touchpad C-21 15.7–16.0 ms, C-23 5.6–12.9 ms (unchanged) | **Keep** |
| 3a | Anchor (R-23) | `MagBridge::setTransform` rounds offsets (`lround`) instead of truncating; offsets were already recomputed from the float anchor every frame | 29 → 13 | C-15 anchor/floor p95 1.88–1.97 → 0.95–0.98; C-16 max ~2.0 → 0.98–1.00 (all 8 pass). C-18 snap-backs unchanged as predicted (8–50 %) | **Keep** |

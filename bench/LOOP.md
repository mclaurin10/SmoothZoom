# Tuning loop log — TEMPLAPTOP (AC), session 3

Branch `perf/laptop-tuning` (from `feature/shift-scroll`). Yardstick frozen at tag `loop-start`:
`bench/criteria.json`, `tools/bench/`, `bench/traces/`, `src/app/DevTools.{cpp,h}`,
`include/smoothzoom/common/DevTelemetry.h`. Every run uses `--config` (never the real config).

Per iteration: hypothesis → change → `tools\bench\gate.ps1 -Quick` → commit with before/after →
keep or revert. Full gate (5 runs/trace) runs on every stop attempt (Stop hook, `stop-gate.ps1`).

| # | Item | Change | Quick gate: fails before → after | Key numbers | Decision |
|---|------|--------|----------------------------------|-------------|----------|
| 1 | Cold start (R-24) | `MagBridge::warmUp()` at render-thread init (after `initComplete_`, skipped on magnifier conflict): `MagSetFullscreenTransform(1.0001)` then `1.0` — pays DWM's one-time magnifier setup (165–188 ms, logged) at launch instead of on the first zoom | 49 → 42 | C-11 cold start 159–161 ms → 0.05–0.16 ms on all 8 traces; C-12 unchanged (15.1–16.0 ms). One late tick (26 ms, mid-trace, 1 run) on touchpad-edge = scheduler noise | **Keep** |

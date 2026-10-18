# Touchpad gestures, per-device settings: verification record (TEMPLAPTOP, AC, 2026-10-02)

Branch `feature/touchpad-gestures` (from `perf/laptop-tuning` f8bfe59), not pushed.

## Smoothness gate: same environment, back to back (5 runs/trace)

| Gate | Code | Failing | Time |
|---|---|---|---|
| Branch | 4613c37 (before the review fixes; they do not touch these paths) | 12 / 172 | 14:17–14:28 |
| Base | perf/laptop-tuning f8bfe59 | 12 / 172 | 14:28–14:40 |

Outputs: `baseline/TEMPLAPTOP-AC/gate-touchpad-gestures-{branch,base}.json`. AC, no R-25 stalls.

Failing checks are the same set as session 3's final gate (touchpad holes on tp-edge/flick/terminal, C-18 snap-backs), except for two C-18 values that crossed the 15 % threshold in opposite directions:

| Check | Base | Branch |
|---|---|---|
| shift-touchpad-edge C-18 | 13.1 (pass) | 15.08 (fail) |
| shift-wheel-sweep C-18 | 15.09 (fail) | 14.9 (pass) |

tp-edge is pure PTP input (Edge), and its zoom path is the same on both builds. A same-environment rerun of tp-edge (5 runs each, `results/rerun-edge-{base,branch}`) gave medians of 12.7 (base) and 13.0 (branch), both passing, with per-run spreads of 12.1–17.5 and 11.8–16.3. Session 3's final gate scored 17 on the same trace. So the flip is threshold noise, not a code effect.

Other values (median of runs, base → branch): tp-flick C-05 10.2 → 10.6 %, tp-terminal C-05 2.6 → 2.3 %, tp-terminal C-08 4.0 → 4.9 P (input-limited holes), wheel-flick C-18 20.0 → 21.5 %, detented C-18 30.7 → 26.3 %. Behaviour differs from base in one recorded trace: in shift-wheel-flick the user released Shift while the wheel was still turning. Those last notches now zoom (mode lock, AC-2.1.26) instead of scrolling the page.

## Scroll-gesture check

`tools/bench/check-gestures.ps1`: 40/40 (coast on and off; `baseline/TEMPLAPTOP-AC/gestures-check.json`).

## Live recordings (`traces-recorded-gestures/`, Windows Terminal, natural scrolling on)

- `touch-stop-coast.csv`: Windows keeps sending a coast's wheel events for 15–100 ms after a new touch (about half of 60 touches). The mode lock held in all 19 gestures where Shift changed mid-gesture.
- `touch-stop-validate.csv` (touch-to-stop build): leftovers after a touch during a zoom coast were all swallowed; leftovers after a touch during a page coast reached the app; the new stroke took the modifier at its start. Replay matches live consume/pass on all 858 wheel events.

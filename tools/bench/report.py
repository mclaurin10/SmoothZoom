"""Render a markdown metrics table from a run-bench.ps1 aggregate.json.

Usage:  python tools/bench/report.py bench/baseline/TEMPLAPTOP-AC/aggregate.json [--out table.md]
Each cell is the median across runs, with (min-max) when runs differ.
"""
import argparse
import json
import statistics as st

ROWS = [
    ("refresh period (ms)", "refresh_period_ms", 3, None),
    ("transform updates", "counts.updates", 0, None),
    ("update interval p50 (periods)", "update_interval_periods.p50", 2, None),
    ("update interval p95 (periods)", "update_interval_periods.p95", 2, None),
    ("update interval p99 (periods)", "update_interval_periods.p99", 2, None),
    ("update interval max (periods)", "update_interval_periods.max", 2, None),
    ("update interval p50 / max (ms)", "update_interval_ms.p50", 1, "update_interval_ms.max"),
    ("refreshes during gesture", "refreshes.active", 0, None),
    ("refreshes missed during gesture", "refreshes.missed", 0, None),
    ("missed (%)", "refreshes.missed_pct", 1, None),
    ("refreshes with >1 update", "refreshes.multi_update", 0, None),
    ("render tick interval p95 / max (ms)", "tick_interval_ms.p95", 1, "tick_interval_ms.max"),
    ("late ticks (>1.5 periods)", "late_ticks", 0, None),
    ("update phase after vblank p50 (ms)", "vblank_phase_ms.p50", 1, None),
    ("MagSetFullscreenTransform call max (ms)", "set_call_ms.max", 3, None),
    ("cold-start first transform (ms)", "cold_start.max_set_call_ms", 0, None),
    ("anchor error p95 / max (phys px)", "anchor_error_px.p95", 2, "anchor_error_px.max"),
    ("  of which offset truncation p95", "anchor_quantization_px.p95", 2, None),
    ("round-trip error (%)", "roundtrip_error_pct_max", 5, None),
    ("input to transform update p50 / p95 (ms)", "latency_ms.input_to_update.p50", 1, "latency_ms.input_to_update.p95"),
    ("input to next vblank p50 / p95 (ms)", "latency_ms.input_to_next_vblank.p50", 1, "latency_ms.input_to_next_vblank.p95"),
    ("settle after last input max (ms)", "settle_ms.max", 1, None),
    ("per-refresh zoom step p50 / max (ln)", "zoom_step_log.p50", 3, "zoom_step_log.max"),
    ("zoom max / end", "zoom.max", 2, "zoom.end"),
]


def get(d, path):
    for k in path.split("."):
        if not isinstance(d, dict):
            return None
        d = d.get(k)
    return d


def cell(runs, path, dp):
    vals = [v for v in (get(r, path) for r in runs) if isinstance(v, (int, float)) and not isinstance(v, bool)]
    if not vals:
        return "n/a"
    fmt = (lambda v: f"{v:.{dp}f}") if dp else (lambda v: f"{v:.0f}")
    med, lo, hi = st.median(vals), min(vals), max(vals)
    return fmt(med) if fmt(lo) == fmt(hi) else f"{fmt(med)} ({fmt(lo)}–{fmt(hi)})"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("aggregate")
    ap.add_argument("--out")
    a = ap.parse_args()
    agg = json.load(open(a.aggregate, encoding="utf-8-sig"))
    names = list(agg["traces"].keys())
    lines = ["| Metric | " + " | ".join(names) + " |", "|---|" + "---|" * len(names)]
    for label, path, dp, second in ROWS:
        cells = []
        for n in names:
            runs = agg["traces"][n]["runs"]
            c = cell(runs, path, dp)
            if second:
                c += " / " + cell(runs, second, dp)
            cells.append(c)
        lines.append(f"| {label} | " + " | ".join(cells) + " |")
    text = "\n".join(lines) + "\n"
    if a.out:
        open(a.out, "w", encoding="utf-8", newline="\n").write(text)
    print(text)


if __name__ == "__main__":
    main()

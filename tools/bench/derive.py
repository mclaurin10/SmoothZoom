#!/usr/bin/env python3
"""SmoothZoom bench: metrics derived from a run's raw telemetry (frozen yardstick).

    python tools/bench/derive.py <run dir> [--simulate round|trunc] [--out FILE]

Reads <run dir>/frames.csv and events.csv (written by `SmoothZoom.exe --bench`)
and writes <run dir>/derived.json. These metrics are computed here, in the
frozen tools/bench/ folder, from the raw per-frame columns, so app code under
tuning cannot change how they are scored:

  anchor_rel        Anchor error relative to the whole-pixel floor. Integer
                    viewport offsets put the desktop point under the pointer up
                    to 0.5 desktop px (= 0.5 x zoom physical px) from where
                    proportional mapping wants it, per axis. Value per update =
                    max over axes of |ideal - applied| / 0.5, where ideal =
                    committed_pointer * (1 - 1/zoom) (clamped like
                    ViewportTracker::computePointerOffset). 1.0 = exactly the
                    rounding floor; truncation reaches 2.0; 0 = exact.
                    The committed pointer is re-simulated here from the raw
                    cursor with RenderLoop's deadzone (3*monH/1080 px), so
                    deliberate deadzone lag is not counted as anchor error; it is
                    reported separately as anchor_deadzone_px.
  anchor_snapback   Reversals of the anchor's frame-to-frame motion during a
                    gesture. e_k = zoom * (ideal - applied) per axis (physical px
                    on screen, signed); m_k = e_k - e_{k-1} between consecutive
                    pointer-source updates with the pointer still. A snap-back is
                    a step with |m| >= 1 physical px whose sign is opposite to the
                    previous such step on that axis. pct = snap-back steps / still
                    update pairs; per_s = per second of active gesture.
  gesture           Pacing and latency per gesture, same definitions as DevTools
                    computeSummary (missed refreshes, multi-update refreshes,
                    update intervals, input->update, input->next vblank, settle)
                    with two fixes: gestures are grouped on the trace's own
                    timestamps (deterministic across runs), and they are
                    bound-aware: refreshes/intervals while the zoom is pinned at
                    1x or 10x and inputs that left ZoomController's target
                    unchanged (pushing into a bound) are not scored.
                    --raw-gestures turns the bound handling off (parity check).
    .zoom_wobble    Direction reversals of the displayed zoom inside a gesture
                    while the input direction did not change (|step| >= 0.01 %).
    .half_response_ms  For an isolated input step (no target change in the prior
                    50 ms): input -> first update that has covered half of the
                    log-zoom step.
  stall             R-25 signature: a render-tick gap >= 100 ms during which DWM's
                    cRefresh advanced < half the expected refreshes AND an input
                    scheduled inside the gap was injected >= 50 ms late (the main
                    thread, which runs the hooks and the injector, was paused
                    too). Gaps without a scheduled input are listed but not
                    classified as R-25 (main thread unobserved).
  env               Validity: DWM refresh timing present.

Applied zoom/offsets come from the MagGetFullscreenTransform read-back columns
(rb_zoom, rb_x, rb_y) when frames.csv has them, else from zoom/applied_x/y.
--simulate replaces the applied offsets with round()/trunc() of the ideal
(what-if scoring of existing logs; never used by the gate).
"""
import argparse, bisect, csv, ctypes, json, math, os, sys

TAU_PX = 1.0            # smallest anchor step counted as motion (physical px)
STALL_GAP_MS = 100.0    # render-tick gap that can be an R-25 stall
STALL_LATE_MS = 50.0    # injector lateness that shows the main thread paused
BURST_GAP_MS = 100.0    # same gesture definition as DevTools computeSummary


def pct(v, p):
    if not v:
        return None
    s = sorted(v)
    r = p / 100.0 * (len(s) - 1)
    lo, hi = math.floor(r), math.ceil(r)
    return s[lo] + (s[hi] - s[lo]) * (r - lo)


def stats(v):
    if not v:
        return {"n": 0, "p50": None, "p95": None, "p99": None, "max": None}
    return {"n": len(v), "p50": pct(v, 50), "p95": pct(v, 95), "p99": pct(v, 99), "max": max(v)}


def screen_size():
    """Primary monitor size in physical px (the bench machine, single monitor)."""
    try:
        u = ctypes.windll.user32
        try:
            u.SetProcessDpiAwarenessContext(ctypes.c_void_p(-4))  # PER_MONITOR_AWARE_V2
        except Exception:
            u.SetProcessDPIAware()
        return u.GetSystemMetrics(0), u.GetSystemMetrics(1)
    except Exception:
        return 1920, 1200


def prop(c, z, size):
    """ViewportTracker::computePointerOffset for origin 0 (float math in double)."""
    if z <= 1.0:
        return 0.0
    k = 1.0 - 1.0 / z
    return min(max(c * k, 0.0), size * k)


def load(run_dir):
    with open(os.path.join(run_dir, "frames.csv"), newline="") as f:
        frames = list(csv.DictReader(f))
    with open(os.path.join(run_dir, "events.csv"), newline="") as f:
        events = list(csv.DictReader(f))
    return frames, events


ZOOM_MIN, ZOOM_MAX = 1.0, 10.0   # --bench runs on built-in default settings
PIN_EPS = 1e-4


def gesture_metrics(F, measured, events, P, bounds_aware=True, zkey="zoom"):
    """Pacing/latency metrics per gesture (DevTools computeSummary definitions),
    made bound-aware: while the zoom is pinned at min/max and input pushes into
    the bound, nothing should change, so those refreshes, intervals and inputs
    are not scored. bounds_aware=False reproduces the C++ numbers exactly."""
    upd = [d for d in measured if d["updated"]]
    for d in measured:
        z, tz = d[zkey], d["target_zoom"]
        d["pinned"] = any(abs(z - b) < PIN_EPS and abs(tz - b) < PIN_EPS for b in (ZOOM_MIN, ZOOM_MAX))

    def refresh_of(d, t):
        if P <= 0 or d["vblank_ms"] == 0:
            return math.floor(t / (P if P > 0 else 1))
        return d["c_refresh"] + math.floor((t - d["vblank_ms"]) / P)

    for d in upd:
        d["refresh"] = refresh_of(d, d["set_begin_ms"])
    starts = [d["set_begin_ms"] for d in upd]

    # Consumed inputs; an input is effective unless the frame that consumed it
    # left ZoomController's target unchanged (pushing into a bound).
    ticks = [d["t_ms"] for d in measured]
    ins = []
    for e in events:
        if e["source"] == "MARK" or e["consumed"] != "1":
            continue
        t = float(e["inject_ms"])
        k = bisect.bisect_left(ticks, t)
        eff = True
        if bounds_aware and 0 < k < len(measured):
            eff = abs(measured[k]["target_zoom"] - measured[k - 1]["target_zoom"]) > 1e-7 or measured[k]["scroll_delta"] == 0
        ins.append((t, eff, float(e["trace_t_us"]) / 1000.0))
    ins.sort()
    # Gestures are grouped on the TRACE timestamps (exact, identical in every
    # run), not on injection times: human notches ~100 ms apart would otherwise
    # flip between one and two gestures with injection jitter.
    bursts = []
    for i, (t, _, tt) in enumerate(ins):
        if bursts and tt - ins[bursts[-1][1]][2] <= BURST_GAP_MS:
            bursts[-1][1] = i
        else:
            bursts.append([i, i])

    interval_ms, interval_p, settle, lat_upd, lat_vbl = [], [], [], [], []
    active = missed = no_update = 0
    for b, (i0, i1) in enumerate(bursts):
        b_start, b_end = ins[i0][0], (ins[bursts[b + 1][0]][0] if b + 1 < len(bursts) else float("inf"))
        u0 = bisect.bisect_left(starts, b_start)
        u1 = u0
        while u1 < len(upd) and upd[u1]["set_begin_ms"] < b_end:
            u1 += 1
        if u1 == u0:
            continue
        r0, r1 = upd[u0]["refresh"], upd[u1 - 1]["refresh"]
        got = {u["refresh"] for u in upd[u0:u1]}
        excl = set()
        if bounds_aware:
            # Refreshes whose newest frame shows the zoom pinned at a bound.
            last_frame = {}
            for d in measured:
                if upd[u0]["t_ms"] <= d["t_ms"] <= upd[u1 - 1]["set_begin_ms"]:
                    last_frame[refresh_of(d, d["t_ms"])] = d
            excl = {r for r, d in last_frame.items() if d["pinned"] and r not in got}
        span = [r for r in range(r0, r1 + 1) if r not in excl]
        active += len(span)
        missed += sum(1 for r in span if r not in got)
        for k in range(u0 + 1, u1):
            if bounds_aware and upd[k - 1]["pinned"]:
                continue
            dms = upd[k]["set_begin_ms"] - upd[k - 1]["set_begin_ms"]
            interval_ms.append(dms)
            if P > 0:
                interval_p.append(dms / P)
        eff_in = [ins[i][0] for i in range(i0, i1 + 1) if ins[i][1]]
        last_in = eff_in[-1] if eff_in else ins[i1][0]
        settle.append(max(0.0, upd[u1 - 1]["set_begin_ms"] - last_in))
        for i in range(i0, i1 + 1):
            t, eff, _ = ins[i]
            if not eff:
                continue
            k = bisect.bisect_left(starts, t)
            if k >= u1:
                no_update += 1
                continue
            lat_upd.append(upd[k]["set_begin_ms"] - t)
            d = upd[k]
            if P > 0 and d["vblank_ms"] != 0:
                v_next = d["vblank_ms"] + (math.floor((d["set_end_ms"] - d["vblank_ms"]) / P) + 1) * P
                lat_vbl.append(v_next - t)

    # Zoom wobble: the displayed zoom reverses direction inside a gesture while
    # the input direction did not change. Smooth animation is monotone; this
    # catches per-frame zoom adjustments that trade anchor shimmer for a
    # visibly pumping zoom.
    last_sign = 0
    for d in measured:
        if d["scroll_delta"]:
            last_sign = 1 if d["scroll_delta"] > 0 else -1
        d["in_sign"] = last_sign
    wobble = moving = 0
    for b, (i0, i1) in enumerate(bursts):
        b_start, b_end = ins[i0][0], (ins[bursts[b + 1][0]][0] if b + 1 < len(bursts) else float("inf"))
        g = [u for u in upd if b_start <= u["set_begin_ms"] < b_end]
        dir_sign, dir_in = 0, 0
        for a_, b_ in zip(g, g[1:]):
            dz = math.log(b_[zkey] / a_[zkey]) if a_[zkey] > 0 and b_[zkey] > 0 else 0.0
            if abs(dz) < 1e-4:                  # < 0.01 %: not visible motion
                continue
            s = 1 if dz > 0 else -1
            moving += 1
            if dir_sign and s != dir_sign and b_["in_sign"] == dir_in:
                wobble += 1
            dir_sign, dir_in = s, b_["in_sign"]

    # Half-response: for an isolated input step (no target change in the
    # previous 50 ms), time from the input to the first update that has covered
    # half of the log-zoom step, provided no other step intervenes. Direct
    # application scores one update; an animation must not lag the input.
    half = []
    upd_t = [u["t_ms"] for u in upd]
    prev_change = -1e18
    eff_t = [t for t, e, _ in ins if e]
    for k in range(1, len(measured)):
        d0, d1 = measured[k - 1], measured[k]
        if d0["target_zoom"] <= 0 or d1["target_zoom"] <= 0:
            continue
        if abs(math.log(d1["target_zoom"] / d0["target_zoom"])) < 1e-6:
            continue
        isolated = d1["t_ms"] - prev_change >= 50.0
        prev_change = d1["t_ms"]
        if not isolated or d0[zkey] <= 0:
            continue
        j = bisect.bisect_right(eff_t, d1["t_ms"]) - 1
        if j < 0 or d1["t_ms"] - eff_t[j] > 2 * max(P, 1.0):
            continue
        start, goal = math.log(d0[zkey]), math.log(d1["target_zoom"])
        mid = (start + goal) / 2
        for m in range(k, len(measured)):
            dm = measured[m]
            if m > k and abs(math.log(dm["target_zoom"] / measured[m - 1]["target_zoom"])) >= 1e-6:
                break                           # another step intervened
            if dm["updated"] and dm[zkey] > 0 and (math.log(dm[zkey]) - mid) * (goal - start) >= 0:
                half.append(dm["set_begin_ms"] - eff_t[j])
                break

    per_refresh = {}
    for d in upd:
        per_refresh[d["refresh"]] = per_refresh.get(d["refresh"], 0) + 1
    return {
        "bounds_aware": bounds_aware,
        "inputs": len(ins), "inputs_ineffective": sum(1 for x in ins if not x[1]), "bursts": len(bursts),
        "refreshes": {"active": active, "missed": missed,
                      "missed_pct": 100.0 * missed / active if active else 0.0,
                      "multi_update": sum(1 for v in per_refresh.values() if v > 1)},
        "update_interval_ms": stats(interval_ms),
        "update_interval_periods": stats(interval_p),
        "latency_ms": {"input_to_update": stats(lat_upd), "input_to_next_vblank": stats(lat_vbl),
                       "inputs_without_update": no_update},
        "settle_ms": stats(settle),
        "zoom_wobble": {"count": wobble, "moving_pairs": moving,
                        "pct": 100.0 * wobble / moving if moving else 0.0},
        "half_response_ms": stats(half),
    }


def derive(run_dir, simulate=None, raw_gestures=False):
    frames, events = load(run_dir)
    W, H = screen_size()
    deadzone = max(1, 3 * H // 1080)
    has_rb = bool(frames) and "rb_zoom" in frames[0]

    F = []
    for r in frames:
        d = {k: float(r[k]) for k in ("t_ms", "set_begin_ms", "set_end_ms", "vblank_ms", "period_ms", "zoom",
                                      "target_zoom")}
        d.update({k: int(float(r[k])) for k in ("c_refresh", "applied_x", "applied_y", "cursor_x", "cursor_y",
                                                  "scroll_delta", "source", "updated", "ok")})
        if has_rb:
            d["rb_zoom"] = float(r["rb_zoom"]); d["rb_x"] = int(float(r["rb_x"])); d["rb_y"] = int(float(r["rb_y"]))
        # Idle 1.0x short-circuit rows carry no cursor (DevTools logs them for cadence only).
        d["idle"] = (d["cursor_x"] == 0 and d["cursor_y"] == 0 and d["updated"] == 0 and d["zoom"] == 1.0)
        F.append(d)

    # Committed pointer: RenderLoop deadzone, run over every non-idle frame in order.
    cx = cy = None
    for d in F:
        if d["idle"]:
            d["cx"], d["cy"] = cx, cy
            continue
        if cx is None:
            cx, cy = d["cursor_x"], d["cursor_y"]
        dx, dy = d["cursor_x"] - cx, d["cursor_y"] - cy
        if dx * dx + dy * dy > deadzone * deadzone:
            cx, cy = d["cursor_x"], d["cursor_y"]
        d["cx"], d["cy"] = cx, cy

    measured = [d for d in F if d["t_ms"] >= -200.0]   # DevTools excludes the warm-up the same way
    upd = [d for d in measured if d["updated"]]

    def applied(d):
        z = d["rb_zoom"] if has_rb else d["zoom"]
        ix, iy = prop(d["cx"], z, W), prop(d["cy"], z, H)
        if simulate == "round":
            ax, ay = math.floor(ix + 0.5), math.floor(iy + 0.5)
        elif simulate == "trunc":
            ax, ay = int(ix), int(iy)
        elif has_rb:
            ax, ay = d["rb_x"], d["rb_y"]
        else:
            ax, ay = d["applied_x"], d["applied_y"]
        return z, ix, iy, ax, ay

    # Gestures from consumed inputs (as DevTools: gap <= 100 ms).
    inputs = sorted(float(e["inject_ms"]) for e in events if e["source"] != "MARK" and e["consumed"] == "1")
    bursts = []
    for t in inputs:
        if bursts and t - bursts[-1][1] <= BURST_GAP_MS:
            bursts[-1][1] = t
        else:
            bursts.append([t, t])

    rel, dz = [], []
    for d in upd:
        if d["source"] != 0 or d["cx"] is None:
            continue
        z, ix, iy, ax, ay = applied(d)
        if z <= 1.0:
            continue
        rel.append(max(abs(ix - ax), abs(iy - ay)) / 0.5)
        dz.append(math.hypot(prop(d["cursor_x"], z, W) - ix, prop(d["cursor_y"], z, H) - iy) * z)

    snap = pairs = 0
    active_s = 0.0
    for bi, (b0, _) in enumerate(bursts):
        b_end = bursts[bi + 1][0] if bi + 1 < len(bursts) else float("inf")
        g = [d for d in upd if b0 <= d["set_begin_ms"] < b_end]
        if len(g) < 2:
            continue
        active_s += (g[-1]["set_begin_ms"] - g[0]["set_begin_ms"]) / 1000.0
        last_dir = [0, 0]
        prev = None
        for d in g:
            ok = d["source"] == 0 and d["cx"] is not None
            z, ix, iy, ax, ay = applied(d) if ok else (0, 0, 0, 0, 0)
            ok = ok and z > 1.0
            cur = (z * (ix - ax), z * (iy - ay), d["cursor_x"], d["cursor_y"], d["cx"], d["cy"]) if ok else None
            if cur is None or prev is None or cur[2:] != prev[2:]:
                last_dir = [0, 0]            # pointer moved / source changed: new reference
                prev = cur
                continue
            pairs += 1
            hit = False
            for a in (0, 1):
                m = cur[a] - prev[a]
                if abs(m) >= TAU_PX:
                    s = 1 if m > 0 else -1
                    if last_dir[a] and s != last_dir[a]:
                        hit = True
                    last_dir[a] = s
            snap += hit
            prev = cur

    # Environment validity and R-25 stall signature.
    periods = sorted(d["period_ms"] for d in measured if d["period_ms"] > 0)
    P = periods[len(periods) // 2] if periods else 0.0
    env = {"ok": P > 0 and any(d["vblank_ms"] != 0 for d in measured), "refresh_period_ms": P}
    if not env["ok"]:
        env["reason"] = "DWM reported no refresh timing (display off, session locked or DWM not composing)"

    sched = []
    for e in events:
        try:
            sched.append((float(e["trace_t_us"]) / 1000.0, float(e["inject_ms"])))
        except ValueError:
            pass
    r25, idle_gaps, max_late = [], [], 0.0
    for s_ms, i_ms in sched:
        max_late = max(max_late, i_ms - s_ms)
    for a, b in zip(measured, measured[1:]):
        gap = b["t_ms"] - a["t_ms"]
        if gap < STALL_GAP_MS or P <= 0:
            continue
        expected = gap / P
        adv = b["c_refresh"] - a["c_refresh"]
        dwm_paused = adv < 0.5 * expected
        inside = [i - s for s, i in sched if a["t_ms"] <= s <= b["t_ms"]]
        item = {"t_ms": round(a["t_ms"], 1), "gap_ms": round(gap, 1), "refresh_advance": adv,
                "refresh_expected": round(expected, 1), "zoom": a["zoom"],
                "main_late_ms": round(max(inside), 1) if inside else None}
        if dwm_paused and inside and max(inside) >= STALL_LATE_MS:
            r25.append(item)
        else:
            idle_gaps.append(item)

    gest = gesture_metrics(F, measured, events, P, bounds_aware=not raw_gestures, zkey="rb_zoom" if has_rb else "zoom")

    return {
        "schema": 1,
        "gesture": gest,
        "simulate": simulate,
        "screen": [W, H], "deadzone_px": deadzone,
        "anchor_source": "simulate-" + simulate if simulate else ("readback" if has_rb else "telemetry"),
        "anchor_rel": stats(rel),
        "anchor_deadzone_px": stats(dz),
        "anchor_snapback": {"count": snap, "pairs": pairs, "tau_px": TAU_PX,
                            "pct": (100.0 * snap / pairs) if pairs else None,
                            "per_s": (snap / active_s) if active_s > 0 else None,
                            "active_s": active_s},
        "stall": {"r25": bool(r25), "r25_gaps": r25, "other_gaps": idle_gaps,
                  "max_inject_late_ms": round(max_late, 1)},
        "env": env,
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("run_dir")
    ap.add_argument("--simulate", choices=["round", "trunc"])
    ap.add_argument("--raw-gestures", action="store_true",
                    help="gesture metrics without the bound exclusions (must equal DevTools summary.json)")
    ap.add_argument("--out")
    a = ap.parse_args()
    res = derive(a.run_dir, a.simulate, a.raw_gestures)
    out = a.out or (None if (a.simulate or a.raw_gestures) else os.path.join(a.run_dir, "derived.json"))
    if out:
        with open(out, "w") as f:
            json.dump(res, f, indent=2)
    json.dump(res, sys.stdout, indent=2)
    print()


if __name__ == "__main__":
    main()

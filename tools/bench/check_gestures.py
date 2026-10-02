"""Check the scroll-gesture bench runs written by tools/bench/check-gestures.ps1.

Usage:  python tools/bench/check_gestures.py <out root>

<out root>/coast-on/<trace>/run1/ and <out root>/coast-off/<trace>/run1/ each
hold a --bench run (events.csv, frames.csv). For every run this checks, per
trace (AC-2.1.25-31):
  - which wheel events reached the application (consumed = 0) - none may leak
    out of a zoom gesture, and every event of a page-scroll gesture must;
  - the gesture verdict (action column: zoom / swallow / pass);
  - whether the zoom moved where it must not (frames.csv target_zoom).
Prints one line per check and exits 1 if any fails. Writes gestures.json.
"""
import csv
import json
import os
import sys


def load(run_dir):
    with open(os.path.join(run_dir, "events.csv"), newline="") as f:
        events = list(csv.DictReader(f))
    with open(os.path.join(run_dir, "frames.csv"), newline="") as f:
        # Measured frames only (t >= 0): the bench's warm-up notch comes before.
        frames = [(float(r["t_ms"]), float(r["target_zoom"]), int(r["scroll_delta"]))
                  for r in csv.DictReader(f) if float(r["t_ms"]) >= 0.0]
    return events, frames


def zoom_at(frames, t_ms):
    z = 1.0
    for t, tz, _ in frames:
        if t > t_ms:
            break
        z = tz
    return z


def applied_sum(frames):
    """Scroll delta the render loop consumed (all frames)."""
    return sum(d for _, _, d in frames)


def zoom_sum(rows):
    """Delta of the events the gesture tracker let zoom (touchpad_natural 0:
    the touchpad direction equals the hook convention)."""
    return sum(int(e["delta"]) for e in rows if e["action"] == "zoom")


def final_zoom(frames):
    return frames[-1][1] if frames else 1.0


def segments(events):
    """Split rows at CONTACT 0 (lift) and CONTACT >0 (touch)."""
    lift = next((i for i, e in enumerate(events) if e["source"] == "CONTACT" and e["delta"] == "0"), None)
    return lift


def wheel_rows(events, lo=0, hi=None):
    hi = len(events) if hi is None else hi
    return [e for e in events[lo:hi] if e["source"] in ("WHEEL", "HWHEEL", "PTP", "RAW")]


def check(trace, coast, events, frames):
    out = []

    def expect(name, ok, detail=""):
        out.append({"trace": trace, "coast": coast, "check": name, "pass": bool(ok), "detail": detail})

    actions = lambda rows: sorted(set(e["action"] for e in rows))
    leaked = lambda rows: sum(1 for e in rows if e["source"] in ("WHEEL", "HWHEEL") and e["consumed"] == "0")

    if trace == "touchpad-zoom-flick-shift-released":
        lift = segments(events)
        finger, coast_rows = wheel_rows(events, 0, lift), wheel_rows(events, lift)
        expect("finger part zooms", actions(finger) == ["zoom"], f"actions {actions(finger)}")
        expect("nothing passes to the app", leaked(finger + coast_rows) == 0,
               f"{leaked(finger + coast_rows)} wheel events passed through")
        t_lift = float(events[lift]["inject_ms"])
        z_lift = zoom_at(frames, t_lift)
        z_end = final_zoom(frames)
        if coast == "on":
            expect("coast zooms (incl. after Shift is released)", actions(coast_rows) == ["zoom"],
                   f"actions {actions(coast_rows)}")
            expect("zoom keeps growing after the lift", z_end > z_lift * 1.05, f"{z_lift:.4f} -> {z_end:.4f}")
        else:
            expect("coast is swallowed", actions(coast_rows) == ["swallow"], f"actions {actions(coast_rows)}")
            expect("zoom stops at the lift: only the finger part is applied",
                   applied_sum(frames) == zoom_sum(finger) and zoom_sum(coast_rows) == 0 and z_end > 1.0,
                   f"applied {applied_sum(frames)}, finger {zoom_sum(finger)}, coast {zoom_sum(coast_rows)}")
    elif trace == "touchpad-page-flick-shift-pressed":
        rows = wheel_rows(events)
        expect("every event is pass-through", actions(rows) == ["pass"], f"actions {actions(rows)}")
        expect("every wheel event reaches the app", leaked(rows) == len(rows), f"{leaked(rows)}/{len(rows)}")
        expect("never zooms", applied_sum(frames) == 0 and max(z for _, z, _ in frames) == 1.0,
               f"applied {applied_sum(frames)}, max {max(z for _, z, _ in frames):.4f}")
    elif trace == "wheel-shift-released":
        rows = wheel_rows(events)
        spin, after = rows[:14], rows[14:]
        expect("whole spin zooms (Shift released after 4 notches)", actions(spin) == ["zoom"],
               f"actions {actions(spin)}")
        expect("coast setting ignored (no swallow on a wheel)", "swallow" not in actions(rows), "")
        expect("spin is a mouse gesture", sorted(set(e["device"] for e in spin)) == ["mouse"], "")
        expect("after a pause, plain scroll passes", actions(after) == ["pass"] and leaked(after) == len(after),
               f"actions {actions(after)}")
        expect("zoomed by the whole spin", final_zoom(frames) > 1.1 ** 13, f"{final_zoom(frames):.3f}")
    elif trace == "touchpad-touch-stops-coast":
        touch = next(i for i, e in enumerate(events)
                     if e["source"] == "CONTACT" and e["delta"] != "0" and i > 0)
        before, after = wheel_rows(events, 0, touch), wheel_rows(events, touch)
        expect("flick and its coast zoom", actions(before) == (["swallow", "zoom"] if coast == "off" else ["zoom"]),
               f"actions {actions(before)}")
        expect("the new touch starts a page-scroll gesture", actions(after) == ["pass"], f"actions {actions(after)}")
        expect("no zoom after the touch: only the flick is applied",
               applied_sum(frames) == zoom_sum(before) and zoom_sum(after) == 0,
               f"applied {applied_sum(frames)}, before {zoom_sum(before)}, after {zoom_sum(after)}")
    else:
        expect("known trace", False, "no expectations for this trace")
    return out


def main():
    root = sys.argv[1]
    results = []
    for coast in ("on", "off"):
        base = os.path.join(root, f"coast-{coast}")
        if not os.path.isdir(base):
            continue
        for trace in sorted(os.listdir(base)):
            if not os.path.isdir(os.path.join(base, trace)):
                continue
            run = os.path.join(base, trace, "run1")
            if not os.path.exists(os.path.join(run, "events.csv")):
                results.append({"trace": trace, "coast": coast, "check": "bench ran", "pass": False, "detail": run})
                continue
            events, frames = load(run)
            results += check(trace, coast, events, frames)
    for r in results:
        print(f"{'PASS' if r['pass'] else 'FAIL'}  coast {r['coast']:<3}  {r['trace']:<36} {r['check']}"
              + (f"  ({r['detail']})" if r["detail"] else ""))
    with open(os.path.join(root, "gestures.json"), "w") as f:
        json.dump(results, f, indent=2)
    failed = [r for r in results if not r["pass"]]
    print(f"{len(results) - len(failed)}/{len(results)} gesture checks pass")
    sys.exit(1 if failed or not results else 0)


if __name__ == "__main__":
    main()

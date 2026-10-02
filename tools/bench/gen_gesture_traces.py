"""Generate the scroll-gesture traces in bench/gesture-traces/ (AC-2.1.25-31).

These check the per-gesture mode lock and the touchpad coast setting, not
smoothness. They live outside bench/traces/ on purpose: the smoothness gate
(tools/bench/gate.ps1 -All) benches every trace under bench/traces/, and these
traces need a build that understands CONTACT rows. Run them with
tools/bench/check-gestures.ps1, which benches each one with the coast setting
on and off and checks the verdicts (tools/bench/check_gestures.py).

Shapes follow the TEMPLAPTOP recordings (bench/hardware/TEMPLAPTOP.md,
bench/traces/recorded/): touchpad WM_MOUSEWHEEL every ~7 ms while two fingers
move, then the Windows coast (inertia) decaying over ~250 ms after the lift.
Contacts are CONTACT rows (delta = fingers down). Deterministic.

  touchpad-zoom-flick-shift-released  Shift+flick; Shift released 5 events into
                                      the coast (leak 1: the coast used to
                                      scroll the page).
  touchpad-page-flick-shift-pressed   flick without Shift; Shift pressed 5 events
                                      into the coast, e.g. to Shift+click
                                      (leak 2: the coast used to zoom).
  wheel-shift-released                mouse wheel: Shift released mid-spin, the
                                      spin goes on 45 ms apart; then a pause and
                                      a plain page scroll.
  touchpad-touch-stops-coast          Shift+flick, Shift released in the coast,
                                      then a new two-finger touch mid-coast that
                                      page-scrolls (PTP rows, no Shift).

Usage:  python tools/bench/gen_gesture_traces.py
"""
import math
import os

COLS = "t_us,source,delta,shift,ctrl,alt,win,x,y"
X, Y = 960, 600


class Trace:
    def __init__(self, name, desc):
        self.name, self.desc = name, desc
        self.t_us = 0
        self.rows = []

    def wait_ms(self, ms):
        self.t_us += int(round(ms * 1000))

    def row(self, source, delta, shift):
        self.rows.append((self.t_us, source, int(delta), shift, 0, 0, 0, X, Y))

    def write(self, out_dir):
        path = os.path.join(out_dir, self.name + ".csv")
        with open(path, "w", newline="\n") as f:
            f.write("# smoothzoom-trace: 1\n# kind: synthetic\n")
            f.write(f"# description: {self.desc}\n")
            f.write("# screen: 1920x1200 origin 0,0\n# modifier_vk: 0xA0\n# touchpad_natural: 0\n")
            f.write("# generator: tools/bench/gen_gesture_traces.py\n")
            f.write(COLS + "\n")
            for r in self.rows:
                f.write(",".join(str(v) for v in r) + "\n")
        print(f"{path}: {len(self.rows)} rows, {self.t_us / 1e6:.2f} s")


def finger_deltas(n, peak):
    # Ramp up and hold, like a two-finger swipe accelerating.
    return [max(2, int(round(peak * min(1.0, (i + 1) / 4)))) for i in range(n)]


def coast_deltas(first, n):
    # Exponential decay of the Windows inertia, ending in single digits.
    return [max(2, int(round(first * math.exp(-i / 9.0)))) for i in range(n)]


def touchpad_flick(t, shift_finger, shift_coast_from, shift_coast_value, sign=+1):
    """Two fingers down, 10 hook events 7 ms apart, lift, 35 coast events."""
    t.row("CONTACT", 2, shift_finger)
    t.wait_ms(14)
    for d in finger_deltas(10, 40):
        t.row("WHEEL", sign * d, shift_finger)
        t.wait_ms(7)
    t.wait_ms(-4)
    t.row("CONTACT", 0, shift_finger)
    t.wait_ms(4)
    for i, d in enumerate(coast_deltas(45, 35)):
        shift = shift_coast_value if i >= shift_coast_from else shift_finger
        t.row("WHEEL", sign * d, shift)
        t.wait_ms(7)


def zoom_flick_shift_released():
    t = Trace("touchpad-zoom-flick-shift-released",
              "Shift+two-finger flick (hook path); Shift released 5 events into the coast")
    t.wait_ms(100)
    touchpad_flick(t, shift_finger=1, shift_coast_from=5, shift_coast_value=0)
    return t


def page_flick_shift_pressed():
    t = Trace("touchpad-page-flick-shift-pressed",
              "two-finger page-scroll flick (hook path); Shift pressed 5 events into the coast")
    t.wait_ms(100)
    touchpad_flick(t, shift_finger=0, shift_coast_from=5, shift_coast_value=1)
    return t


def wheel_shift_released():
    t = Trace("wheel-shift-released",
              "mouse wheel: Shift+spin, Shift released after 4 notches, spin continues; pause; page scroll")
    t.wait_ms(100)
    for i in range(14):
        t.row("WHEEL", 120, 1 if i < 4 else 0)
        t.wait_ms(45)
    t.wait_ms(400)            # pause: the gesture is over
    for _ in range(4):
        t.row("WHEEL", 120, 0)
        t.wait_ms(40)
    return t


def touch_stops_coast():
    t = Trace("touchpad-touch-stops-coast",
              "Shift+flick, Shift released in the coast, new touch mid-coast page-scrolls (PTP rows)")
    t.wait_ms(100)
    t.row("CONTACT", 2, 1)
    t.wait_ms(14)
    for d in finger_deltas(10, 40):
        t.row("WHEEL", d, 1)
        t.wait_ms(7)
    t.wait_ms(-4)
    t.row("CONTACT", 0, 1)
    t.wait_ms(4)
    for i, d in enumerate(coast_deltas(45, 12)):
        t.row("WHEEL", d, 1 if i < 3 else 0)
        t.wait_ms(7)
    # New touch while the coast would still be running: Windows stops sending
    # it (on TEMPLAPTOP, tails cut off when a new stroke starts), and the new
    # two-finger motion arrives on the PTP path (hook silent > 50 ms).
    t.row("CONTACT", 2, 0)
    t.wait_ms(10)
    for d in finger_deltas(12, 6):
        t.row("PTP", d, 0)
        t.wait_ms(8)
    t.wait_ms(-4)
    t.row("CONTACT", 0, 0)
    return t


def main():
    root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    out_dir = os.path.join(root, "bench", "gesture-traces")
    os.makedirs(out_dir, exist_ok=True)
    for tr in (zoom_flick_shift_released(), page_flick_shift_pressed(),
               wheel_shift_released(), touch_stops_coast()):
        tr.write(out_dir)


if __name__ == "__main__":
    main()

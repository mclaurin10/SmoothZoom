"""Generate the synthetic bench traces in bench/traces/synthetic/.

Deterministic: no randomness, so the committed traces are reproducible. Shapes are
taken from what tools/observe-wheel.ps1 recorded on TEMPLAPTOP
(bench/hardware/TEMPLAPTOP.md): detented +/-120 wheel at 15-40 ms spacing, and
precision-touchpad WM_MOUSEWHEEL at ~7 ms spacing with small deltas, a large
first event and an inertia tail.

Round-trip MARKs (delta 1 = begin, 2 = end) bracket segments that stay inside
the zoom band where ZoomController's soft bounds do not attenuate
([1.41x, 7.08x] for min 1, max 10; ZoomController.cpp:35). So any round-trip
error there is drift, not intended AC-2.1.15 behaviour. The 1x->10x->1x sweep is
bracketed end to end on purpose: it measures whether a symmetric sweep returns
to exactly 1.0x.

Usage:  python tools/bench/gen_synthetic.py [--width 1920 --height 1200]
"""
import argparse
import math
import os

COLS = "t_us,source,delta,shift,ctrl,alt,win,x,y"


class Trace:
    def __init__(self, name, desc, x, y):
        self.name, self.desc, self.x, self.y = name, desc, x, y
        self.t_us = 0
        self.rows = []

    def wait_ms(self, ms):
        self.t_us += int(round(ms * 1000))

    def wheel(self, delta, win=1):
        self.rows.append((self.t_us, "WHEEL", int(delta), 0, 0, 0, win, self.x, self.y))

    def mark(self, kind):
        self.rows.append((self.t_us, "MARK", kind, 0, 0, 0, 0, self.x, self.y))

    def write(self, out_dir, width, height):
        path = os.path.join(out_dir, self.name + ".csv")
        with open(path, "w", newline="\n") as f:
            f.write("# smoothzoom-trace: 1\n# kind: synthetic\n")
            f.write(f"# description: {self.desc}\n")
            f.write(f"# screen: {width}x{height} origin 0,0\n# modifier_vk: 0x5B\n")
            f.write("# generator: tools/bench/gen_synthetic.py\n")
            f.write(COLS + "\n")
            for r in self.rows:
                f.write(",".join(str(v) for v in r) + "\n")
        wheel = [r for r in self.rows if r[1] == "WHEEL"]
        print(f"{path}: {len(wheel)} wheel events, net {sum(r[2] for r in wheel)}, "
              f"{self.t_us / 1e6:.2f} s")


def detented_sweep(x, y):
    # Mouse wheel spun at a steady ~30 notches/s: 30 notches 1x -> ~10x, pause,
    # 30 notches back. Spacing cycles through observed 26-38 ms intervals.
    t = Trace("detented-sweep", "detented mouse wheel 1x->10x->1x, 30+30 notches at 26-38 ms", x, y)
    gaps = [28, 35, 30, 38, 26, 33]
    t.wait_ms(100)
    t.mark(1)
    t.wait_ms(100)
    for i in range(30):
        t.wheel(+120)
        t.wait_ms(gaps[i % len(gaps)])
    t.wait_ms(500)
    for i in range(30):
        t.wheel(-120)
        t.wait_ms(gaps[i % len(gaps)])
    t.wait_ms(500)
    t.mark(2)
    return t


def freespin_burst(x, y):
    # Free-spin wheel: 120-unit events fired 2-18 ms apart, decelerating, as a
    # flicked ratchet-less wheel does. Pre-zoom 4 notches into the free band, then
    # a 14-event burst up and a 14-event burst down, then back to 1x.
    t = Trace("freespin-burst", "free-spin wheel bursts: 14 events up / 14 down at 2-18 ms (decelerating)", x, y)
    burst_gaps = [2, 2, 3, 3, 4, 4, 5, 6, 7, 9, 11, 14, 18]
    t.wait_ms(100)
    for _ in range(4):
        t.wheel(+120)
        t.wait_ms(45)
    t.wait_ms(400)
    t.mark(1)
    t.wait_ms(50)
    for sign in (+1, -1):
        for i in range(14):
            t.wheel(sign * 120)
            if i < len(burst_gaps):
                t.wait_ms(burst_gaps[i])
        t.wait_ms(450)
    t.mark(2)
    t.wait_ms(50)
    for _ in range(6):
        t.wheel(-120)
        t.wait_ms(45)
    t.wait_ms(300)
    return t


def ptp_gesture(total, finger_ms=240, tau_ms=110, first_boost=3.0):
    """Delta sequence for one two-finger swipe: a large first event, a finger
    phase with a smooth velocity bump, then an exponentially decaying inertia
    tail down to single digits (the shape recorded on the ELAN0124 pad).
    Integer deltas with carried remainder; the sum is exactly `total`."""
    period = 7.0
    n_finger = int(finger_ms / period)
    weights = []
    for i in range(n_finger):
        u = (i + 0.5) / n_finger
        weights.append(0.6 + math.sin(math.pi * u))   # sustained, peaking mid-swipe
    weights[0] *= first_boost
    k = 0
    while True:
        w = weights[n_finger - 1] * 0.8 * math.exp(-(k + 1) * period / tau_ms)
        weights.append(w)
        k += 1
        if w / sum(weights) * abs(total) < 2.0 or k > 80:
            break
    scale = total / sum(weights)
    deltas, carry = [], 0.0
    for w in weights:
        v = w * scale + carry
        d = int(round(v))
        if d == 0:
            d = 1 if total > 0 else -1
        carry = v - d
        deltas.append(d)
    deltas[-1] += total - sum(deltas)   # exact total
    return deltas


def touchpad(x, y):
    # Precision-touchpad WM_MOUSEWHEEL stream at ~7 ms (6.6-7.4 ms jitter cycle).
    t = Trace("touchpad-ptp", "precision-touchpad style: small deltas ~7 ms apart with inertia tails", x, y)
    jitter = [7.0, 6.6, 7.3, 6.9, 7.4, 6.8]
    j = [0]

    def gesture(deltas):
        for d in deltas:
            t.wheel(d)
            t.wait_ms(jitter[j[0] % len(jitter)])
            j[0] += 1

    t.wait_ms(100)
    gesture(ptp_gesture(+1500))            # 1x -> ~3.3x
    t.wait_ms(400)
    t.mark(1)
    t.wait_ms(50)
    b = ptp_gesture(+900, finger_ms=200)   # ~3.3x -> ~6.6x (inside free band)
    gesture(b)
    t.wait_ms(350)
    gesture([-d for d in b])               # exact mirror back down
    t.wait_ms(400)
    t.mark(2)
    t.wait_ms(50)
    gesture(ptp_gesture(-1600))            # back to 1x (soft floor)
    t.wait_ms(300)
    return t


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--width", type=int, default=1920)
    ap.add_argument("--height", type=int, default=1200)
    ap.add_argument("--out", default=os.path.join(os.path.dirname(__file__), "..", "..", "bench", "traces", "synthetic"))
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    # Off-centre pointer (37% / 37%) so offset math is exercised on both axes.
    x, y = int(a.width * 0.37), int(a.height * 0.37)
    for tr in (detented_sweep(x, y), freespin_burst(x, y), touchpad(x, y)):
        tr.write(os.path.normpath(a.out), a.width, a.height)


if __name__ == "__main__":
    main()

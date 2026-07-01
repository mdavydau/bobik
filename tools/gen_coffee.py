#!/usr/bin/env python3
"""
gen_coffee.py — procedurally draw an ANIMATED "coffee time" face (128x64),
saved as a PNG frame sequence. Feed the folder to make_face.py:

  python3 gen_coffee.py
  python3 make_face.py /tmp/coffee_frames --name coffee01 --fps 10 --no-dither

Look: happy content eyes up top, a chunky coffee mug below, and three steam
wisps that wiggle and rise — Tabbie's "time for a coffee break" mood.
"""
import os, math
from PIL import Image, ImageDraw

W, H = 128, 64
OUT = "/tmp/coffee_frames"
NFRAMES = 14
os.makedirs(OUT, exist_ok=True)


def steam(d, x, base_y, top_y, phase, amp=4):
    # a wavy vertical wisp made of short segments, drifting up with the phase
    pts = []
    n = 14
    for k in range(n + 1):
        f = k / n
        y = base_y + (top_y - base_y) * f
        wob = math.sin((f * 3.0 * math.pi) - phase * 2 * math.pi) * amp * (0.35 + f)
        pts.append((x + wob, y))
    d.line(pts, fill=255, width=2)


def frame(i):
    t = i / NFRAMES
    img = Image.new("L", (W, H), 0)
    d = ImageDraw.Draw(img)

    # --- happy content eyes (upward smiling arcs), one each side ---
    for ex in (30, W - 30):
        d.arc([ex - 11, 20, ex + 11, 42], start=200, end=340, fill=255, width=3)

    # --- coffee mug body (centered, lower half) ---
    mug_l, mug_r, mug_t, mug_b = 48, 84, 34, 60
    d.rounded_rectangle([mug_l, mug_t, mug_r, mug_b], radius=5, outline=255, width=3)
    d.line([mug_l + 5, mug_t + 6, mug_r - 5, mug_t + 6], fill=255, width=2)  # coffee surface
    d.arc([mug_r - 4, mug_t + 4, mug_r + 16, mug_t + 22], start=300, end=60, fill=255, width=3)  # handle

    # --- three rising steam wisps, phase-offset so they shimmer ---
    steam(d, 56, mug_t - 1, 6, (t + 0.00) % 1.0)
    steam(d, 66, mug_t - 1, 2, (t + 0.33) % 1.0)
    steam(d, 76, mug_t - 1, 6, (t + 0.66) % 1.0)

    return img


for i in range(NFRAMES):
    frame(i).save(os.path.join(OUT, f"coffee_{i:03d}.png"))
print(f"wrote {NFRAMES} frames to {OUT}")

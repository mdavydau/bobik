#!/usr/bin/env python3
"""
gen_sweat.py — procedurally draw an ANIMATED "hot / sweating" face (128x64),
saved as a PNG frame sequence. Feed the folder to make_face.py:

  python3 gen_sweat.py
  python3 make_face.py /tmp/sweat_frames --name sweat01 --fps 10 --no-dither

Look: tired half-closed eyes, an open panting mouth that pulses, and big sweat
drops that slide down the temples and fall — matched to Tabbie's bold style.
"""
import os, math
from PIL import Image, ImageDraw

W, H = 128, 64
OUT = "/tmp/sweat_frames"
NFRAMES = 14
os.makedirs(OUT, exist_ok=True)


def teardrop(d, cx, cy, r):
    # round top + point at the bottom (a falling drop)
    d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=255)
    d.polygon([(cx - r, cy), (cx + r, cy), (cx, cy + int(r * 2.2))], fill=255)


def frame(i):
    t = i / NFRAMES
    img = Image.new("L", (W, H), 0)
    d = ImageDraw.Draw(img)

    # --- tired, half-closed eyes: a filled rounded rect with the top half cut ---
    ey, eye_w, eye_h = 24, 30, 20
    for ex in (22, W - 22 - eye_w):
        d.rounded_rectangle([ex, ey, ex + eye_w, ey + eye_h], radius=7, fill=255)
        # heavy upper eyelid (black) drooping down -> "exhausted" look
        lid = 9 + int(2 * math.sin(t * 2 * math.pi))
        d.rectangle([ex - 1, ey - 1, ex + eye_w + 1, ey + lid], fill=0)
        # a downward-sloping tired brow above
        d.line([(ex, ey - 5), (ex + eye_w, ey - 1)] if ex < W // 2
               else [(ex, ey - 1), (ex + eye_w, ey - 5)], fill=255, width=3)

    # --- open panting mouth (pulses open/closed) ---
    mo = 3 + int(4 * (0.5 + 0.5 * math.sin(t * 2 * math.pi * 2)))
    d.ellipse([W // 2 - 12, 50 - mo, W // 2 + 12, 50 + mo], fill=255)

    # --- two sweat drops sliding down the temples, phase-offset, looping ---
    def drop(x0, phase):
        p = (t + phase) % 1.0
        y = int(8 + p * 46)          # slides from forehead down past the chin
        r = 3 if p < 0.85 else 2     # shrinks just before it vanishes
        if p < 0.95:
            teardrop(d, x0, y, r)
    drop(16, 0.0)          # left temple
    drop(W - 16, 0.5)      # right temple, opposite phase

    return img


for i in range(NFRAMES):
    frame(i).save(os.path.join(OUT, f"sweat_{i:03d}.png"))
print(f"wrote {NFRAMES} frames to {OUT}")

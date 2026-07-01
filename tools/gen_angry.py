#!/usr/bin/env python3
"""
gen_angry.py — procedurally draw an ANIMATED angry face (128x64, 1-bit style)
and save it as a PNG frame sequence. Feed the output folder to make_face.py:

  python3 gen_angry.py
  python3 make_face.py /tmp/angry_frames --name angry01 --fps 10 --no-dither

Style: big bold white-on-black eyes with steep angry brows, a gritted frown,
a throbbing forehead vein, and a small angry shake — matched to Tabbie's look.
"""
import os, math
from PIL import Image, ImageDraw

W, H = 128, 64
OUT = "/tmp/angry_frames"
NFRAMES = 12
os.makedirs(OUT, exist_ok=True)

def frame(i):
    t = i / NFRAMES
    img = Image.new("L", (W, H), 0)
    d = ImageDraw.Draw(img)

    # global angry "shake": tiny horizontal jitter
    sx = int(round(1.5 * math.sin(t * 2 * math.pi * 2)))
    # furrow intensity pulses 0..1
    fur = 0.5 + 0.5 * math.sin(t * 2 * math.pi)

    # ---- eyes (two big rounded rects), narrowed as anger rises ----
    eye_w, eye_h = 30, 26 - int(6 * fur)
    ey = 24
    lx = 22 + sx
    rx = W - 22 - eye_w + sx
    for ex in (lx, rx):
        d.rounded_rectangle([ex, ey, ex + eye_w, ey + eye_h], radius=7, fill=255)
        # angry "shadow" notch cut from the inner-top corner
    # carve eyebrows as black slants over the eye tops (angry V)
    brow_drop = int(10 + 8 * fur)
    # left brow: high on outer side, low on inner side
    d.polygon([(lx - 3, ey - 6), (lx + eye_w + 4, ey - 6),
               (lx + eye_w + 4, ey + brow_drop), (lx - 3, ey - 2)], fill=0)
    # right brow: mirror
    d.polygon([(rx - 4, ey - 6), (rx + eye_w + 3, ey - 6),
               (rx + eye_w + 3, ey - 2), (rx - 4, ey + brow_drop)], fill=0)
    # thick white brow line on top of each slant for definition
    d.line([(lx - 2, ey + brow_drop - 2), (lx + eye_w + 3, ey - 4)], fill=255, width=4)
    d.line([(rx - 2, ey - 4), (rx + eye_w + 2, ey + brow_drop - 2)], fill=255, width=4)

    # ---- gritted frown mouth (shaking) ----
    mx, my = W // 2 + sx, 56
    d.line([(mx - 16, my), (mx + 16, my)], fill=255, width=3)
    for gx in range(-12, 13, 6):  # teeth ticks
        d.line([(mx + gx, my - 3), (mx + gx, my + 3)], fill=0, width=1)

    # ---- throbbing anger vein (#) top-right, grows with furrow ----
    vr = 3 + int(3 * fur)
    vx, vy = W - 16, 8
    d.line([(vx - vr, vy), (vx + vr, vy)], fill=255, width=2)
    d.line([(vx, vy - vr), (vx, vy + vr)], fill=255, width=2)
    d.line([(vx - vr, vy - vr), (vx + vr, vy + vr)], fill=255, width=1)
    d.line([(vx - vr, vy + vr), (vx + vr, vy - vr)], fill=255, width=1)

    return img

for i in range(NFRAMES):
    frame(i).save(os.path.join(OUT, f"angry_{i:03d}.png"))
print(f"wrote {NFRAMES} frames to {OUT}")

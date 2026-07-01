#!/usr/bin/env python3
"""Generate frames for a scheduled STATUS reminder face."""
import math
import os
from PIL import Image, ImageDraw

OUT = "/tmp/status_alert_frames"
W, H = 128, 64
NFRAMES = 24

os.makedirs(OUT, exist_ok=True)


def triangle_points(cx, cy, r, angle):
    pts = []
    for i in range(3):
        a = angle - math.pi / 2 + i * 2 * math.pi / 3
        pts.append((cx + math.cos(a) * r, cy + math.sin(a) * r))
    return pts


for i in range(NFRAMES):
    img = Image.new("L", (W, H), 0)
    d = ImageDraw.Draw(img)
    t = i / NFRAMES

    # Pulsing header/footer text.
    if i % 6 < 4:
        d.text((3, 2), "STATUS STATUS", fill=255)
        d.text((18, 54), "STATUS!", fill=255)
    else:
        d.text((24, 2), "WRITE STATUS", fill=255)
        d.text((8, 54), "NOW NOW NOW", fill=255)

    # Rotating warning triangle.
    angle = t * 2 * math.pi
    cx, cy = 64, 32
    outer = triangle_points(cx, cy, 22 + (i % 4), angle)
    inner = triangle_points(cx, cy, 15, angle)
    d.polygon(outer, outline=255)
    d.polygon(inner, outline=255)

    # Exclamation mark inside.
    d.rounded_rectangle((61, 21, 66, 38), radius=2, fill=255)
    d.rectangle((62, 41, 65, 44), fill=255)

    # Spinning corner markers/rays.
    for k in range(8):
        a = angle + k * math.pi / 4
        r1 = 29 if k % 2 else 27
        r2 = 34 if k % 2 else 31
        x1, y1 = cx + math.cos(a) * r1, cy + math.sin(a) * r1
        x2, y2 = cx + math.cos(a) * r2, cy + math.sin(a) * r2
        d.line((x1, y1, x2, y2), fill=255, width=1)

    # Side blinkers.
    if i % 8 < 4:
        d.rectangle((0, 24, 8, 40), outline=255)
        d.rectangle((119, 24, 127, 40), outline=255)
    else:
        d.rectangle((2, 26, 6, 38), fill=255)
        d.rectangle((121, 26, 125, 38), fill=255)

    img.save(os.path.join(OUT, f"status_alert_{i:03d}.png"))

print(f"wrote {NFRAMES} frames to {OUT}")

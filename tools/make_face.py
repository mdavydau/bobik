#!/usr/bin/env python3
"""
make_face.py — turn a GIF / video / image-sequence into a Tabbie OLED
expression header (.h), in the exact Adafruit_GFX format the firmware uses.

USAGE
  python3 make_face.py INPUT --name happy01 [options]

INPUT may be:
  - a .gif file (each frame becomes an animation frame)
  - a video (.mp4/.mov/.webm/...)  — needs ffmpeg on PATH
  - a directory of images (*.png/*.jpg, played in filename order)
  - a single image (a one-frame "animation")

KEY OPTIONS
  --name NAME       base name, lowercase + digits, e.g. happy01   [required]
  --out  PATH       output .h (default: <repo>/firmware/src/<name>.h)
  --fps  N          playback fps stored in the header              (default 12)
  --max-frames N    cap the number of frames                       (default all)
  --fit  MODE       contain | cover | stretch                      (default contain)
  --no-dither       hard threshold instead of Floyd–Steinberg dithering
  --threshold N     0-255 cutoff used with --no-dither             (default 128)
  --invert          flip lit/unlit pixels (use if the face is inverted)

The OLED is 128x64, 1 bit/pixel. Lit (white) pixels = bit set, MSB first,
16 bytes per row, 1024 bytes per frame — identical to idle01.h etc.
"""
import argparse, os, re, sys, shutil, subprocess, tempfile

try:
    from PIL import Image, ImageSequence
except ImportError:
    sys.exit("Pillow is required:  python3 -m pip install Pillow")

W, H = 128, 64
BYTES_PER_FRAME = W * H // 8
VIDEO_EXT = {".mp4", ".mov", ".webm", ".mkv", ".avi", ".m4v"}
IMG_EXT = {".png", ".jpg", ".jpeg", ".bmp", ".gif"}
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))   # <repo>/tools
REPO_ROOT = os.path.dirname(SCRIPT_DIR)                    # <repo>


def load_frames(inp, fps, max_frames):
    """Return a list of PIL images (any mode)."""
    ext = os.path.splitext(inp)[1].lower()
    frames = []

    if os.path.isdir(inp):
        files = sorted(
            f for f in os.listdir(inp)
            if os.path.splitext(f)[1].lower() in IMG_EXT
        )
        if not files:
            sys.exit(f"No images found in directory: {inp}")
        frames = [Image.open(os.path.join(inp, f)) for f in files]

    elif ext == ".gif":
        frames = [f.copy() for f in ImageSequence.Iterator(Image.open(inp))]

    elif ext in VIDEO_EXT:
        if not shutil.which("ffmpeg"):
            sys.exit("ffmpeg not found — install it (brew install ffmpeg) "
                     "or pre-extract frames to a folder and pass that folder.")
        tmp = tempfile.mkdtemp(prefix="makeface_")
        subprocess.run(
            ["ffmpeg", "-loglevel", "error", "-i", inp,
             "-vf", f"fps={fps}", os.path.join(tmp, "f_%05d.png")],
            check=True,
        )
        files = sorted(f for f in os.listdir(tmp) if f.endswith(".png"))
        frames = [Image.open(os.path.join(tmp, f)) for f in files]

    elif ext in IMG_EXT:
        frames = [Image.open(inp)]
    else:
        sys.exit(f"Unsupported input: {inp}")

    if max_frames and len(frames) > max_frames:
        frames = frames[:max_frames]
    return frames


def to_canvas(img, fit):
    """Grayscale + place onto a 128x64 black canvas per the fit mode."""
    img = img.convert("L")
    if fit == "stretch":
        return img.resize((W, H), Image.LANCZOS)

    sw, sh = img.size
    if fit == "cover":
        scale = max(W / sw, H / sh)
    else:  # contain
        scale = min(W / sw, H / sh)
    nw, nh = max(1, round(sw * scale)), max(1, round(sh * scale))
    resized = img.resize((nw, nh), Image.LANCZOS)

    canvas = Image.new("L", (W, H), 0)  # black background
    canvas.paste(resized, ((W - nw) // 2, (H - nh) // 2))
    if fit == "cover":  # crop overflow by pasting onto exact-size canvas (already centered)
        canvas = canvas.crop((0, 0, W, H))
    return canvas


def to_bits(canvas, dither, threshold, invert):
    """Return a 128x64 mode-'1' image (255 = lit pixel)."""
    if dither:
        bw = canvas.convert("1")                       # Floyd–Steinberg
    else:
        bw = canvas.point(lambda p: 255 if p >= threshold else 0).convert("1")
    if invert:
        bw = bw.point(lambda p: 0 if p else 255)
    return bw


def pack(bw):
    """Pack a mode-'1' image into MSB-first bytes, 16 per row."""
    px = bw.load()
    out = bytearray()
    for y in range(H):
        for bx in range(W // 8):
            byte = 0
            for bit in range(8):
                if px[bx * 8 + bit, y]:        # lit -> bit set
                    byte |= 0x80 >> bit
            out.append(byte)
    return out


def build_gif(bw_frames, path, scale, fps):
    """Write an animated GIF preview (amber-on-dark, like a lit OLED)."""
    amber, bg = (247, 182, 90), (8, 10, 13)
    rgb = []
    for bw in bw_frames:
        big = bw.resize((W * scale, H * scale), Image.NEAREST)
        base = Image.new("RGB", big.size, bg)
        lit = Image.new("RGB", big.size, amber)
        rgb.append(Image.composite(lit, base, big))
    rgb[0].save(path, save_all=True, append_images=rgb[1:],
                duration=max(1, round(1000 / fps)), loop=0, disposal=2)


def emit_header(name, frames_bytes, fps):
    NAME = name.upper()
    delay = round(1000 / fps)
    n = len(frames_bytes)
    L = []
    L.append(f"// {name} animation for Adafruit SSD1306/SH1106")
    L.append(f"// Generated by make_face.py - {n} frames @ {fps}fps")
    L.append(f"// Format: Adafruit_GFX bitmap (MSB first)")
    L.append(f"// Display: {W}x{H} OLED")
    L.append("")
    L.append(f"#ifndef {NAME}_H")
    L.append(f"#define {NAME}_H")
    L.append("")
    L.append("#include <Arduino.h>")
    L.append("")
    L.append("// Animation properties")
    L.append(f"#define {NAME}_FRAME_COUNT {n}")
    L.append(f"#define {NAME}_FPS {fps}")
    L.append(f"#define {NAME}_FRAME_DELAY {delay}  // ms per frame")
    L.append("")
    for i, data in enumerate(frames_bytes, 1):
        L.append(f"// Frame {i}")
        L.append(f"const unsigned char PROGMEM {name}_frame_{i:03d}[] = {{")
        for r in range(0, len(data), 16):
            row = ", ".join(f"0x{b:02x}" for b in data[r:r + 16])
            L.append(f"  {row},")
        L.append("};")
        L.append("")
    L.append(f"const unsigned char* const {name}_frames[] PROGMEM = {{")
    L.append(",\n".join(f"  {name}_frame_{i:03d}" for i in range(1, n + 1)))
    L.append("};")
    L.append("")
    L.append("#endif")
    L.append("")
    return "\n".join(L)


def wiring_hints(name):
    NAME = name.upper()
    fn = "draw" + name.capitalize() + "Animation"
    name_base = re.sub(r"\d+$", "", name)
    return f"""
To use this face, wire it into firmware/src/main.cpp (3 edits) then reflash:

  1) near the other includes (~line 48):
       #include "{name}.h"

  2) add a playback function (copy drawRelaxAnimation, swap the names):
       void {fn}() {{
         static int frame = 0;
         unsigned long now = millis();
         if (now - lastFrameTime < {NAME}_FRAME_DELAY) return;
         lastFrameTime = now;
         display.clearBuffer();
         const uint8_t* f = (const uint8_t*)pgm_read_ptr(&{name}_frames[frame]);
         display.drawBitmap(0, 0, 128 / 8, 64, f);
         display.sendBuffer();
         if (++frame >= {NAME}_FRAME_COUNT) frame = 0;
       }}
     (also declare  void {fn}();  with the other prototypes near line 100)

  3) in updateDisplay() (~line 900) add a branch:
       else if (currentAnimation == "{name_base}") {fn}();

  Reflash:   cd firmware && pio run --target upload
  Trigger:   curl -X POST http://tabbie.local/api/animation \\
               -H "Content-Type: application/json" -d '{{"animation":"{name_base}"}}'
"""


def main():
    ap = argparse.ArgumentParser(description="GIF/video/images -> Tabbie OLED .h")
    ap.add_argument("input")
    ap.add_argument("--name", required=True)
    ap.add_argument("--out")
    ap.add_argument("--fps", type=int, default=12)
    ap.add_argument("--max-frames", type=int, default=0)
    ap.add_argument("--fit", choices=["contain", "cover", "stretch"], default="contain")
    ap.add_argument("--no-dither", dest="dither", action="store_false")
    ap.add_argument("--threshold", type=int, default=128)
    ap.add_argument("--invert", action="store_true")
    a = ap.parse_args()

    if not re.fullmatch(r"[a-z][a-z0-9_]*", a.name):
        sys.exit("--name must be lowercase letters/digits, e.g. happy01")

    out = a.out or os.path.join(REPO_ROOT, "firmware", "src", a.name + ".h")

    frames = load_frames(a.input, a.fps, a.max_frames)
    if not frames:
        sys.exit("No frames loaded.")

    packed = []
    for fr in frames:
        canvas = to_canvas(fr, a.fit)
        bw = to_bits(canvas, a.dither, a.threshold, a.invert)
        data = pack(bw)
        assert len(data) == BYTES_PER_FRAME, len(data)
        packed.append(data)

    header = emit_header(a.name, packed, a.fps)
    os.makedirs(os.path.dirname(out), exist_ok=True)
    with open(out, "w") as f:
        f.write(header)

    kb = os.path.getsize(out) / 1024
    print(f"Wrote {out}")
    print(f"  {len(packed)} frames @ {a.fps}fps  ·  {BYTES_PER_FRAME} bytes/frame  ·  {kb:.0f} KB")
    print(f"  fit={a.fit}  dither={a.dither}  invert={a.invert}")
    print(wiring_hints(a.name))


if __name__ == "__main__":
    main()

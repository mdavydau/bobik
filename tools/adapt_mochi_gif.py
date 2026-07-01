#!/usr/bin/env python3
"""
Adapt a Mochi-style GIF into Bobik/Tabbie OLED assets.

This is intentionally a local-preview tool. The known Mochi animation packs are
useful references, but some repos state that the GIF art belongs to Dasai, so
do not commit downloaded source GIFs or derived firmware headers unless you have
the right to use those assets.

Outputs:
  - <out-dir>/<name>.h       firmware header format used by firmware/src/*.h
  - <out-dir>/<name>.json    app preview JSON format
  - <out-dir>/<name>.gif     amber-on-dark OLED preview
  - <out-dir>/<name>_sheet.png contact sheet of sampled frames
"""
import argparse
import json
import os
import re
import sys
import tempfile
import urllib.parse
import urllib.request
from pathlib import Path

try:
    from PIL import Image, ImageDraw, ImageSequence
except ImportError:
    sys.exit("Pillow is required: python3 -m pip install Pillow")

SCRIPT_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(SCRIPT_DIR))

from make_face import W, H, BYTES_PER_FRAME, build_gif, emit_header, pack  # noqa: E402

RAW_BASES = {
    "watcher": "https://raw.githubusercontent.com/pham-tuan-binh/watcher-mochi/main/sd_content/{name}.gif",
    "emote-buddy": "https://raw.githubusercontent.com/tamdilip/emote-buddy/main/assets/gifs/{name}.gif",
    "upiir": "https://raw.githubusercontent.com/upiir/esp32s3_oled_dasai_mochi/main/{name}",
}


def slug(value):
    base = Path(urllib.parse.urlparse(value).path).stem or value
    base = re.sub(r"[^a-zA-Z0-9_]+", "_", base).strip("_").lower()
    if not base or not base[0].isalpha():
        base = "mochi_" + base
    return base


def resolve_input(value, repo):
    parsed = urllib.parse.urlparse(value)
    if parsed.scheme in {"http", "https"}:
        return value
    path = Path(value)
    if path.exists():
        return str(path)
    template = RAW_BASES[repo]
    name = value if repo == "upiir" and value.endswith(".gif") else Path(value).stem
    return template.format(name=name)


def fetch_if_needed(source, cache_dir):
    parsed = urllib.parse.urlparse(source)
    if parsed.scheme not in {"http", "https"}:
        return Path(source)

    cache_dir.mkdir(parents=True, exist_ok=True)
    target = cache_dir / Path(parsed.path).name
    with urllib.request.urlopen(source, timeout=30) as response:
        data = response.read()
    target.write_bytes(data)
    return target


def load_frames(path, max_frames):
    image = Image.open(path)
    frames = [frame.convert("RGBA") for frame in ImageSequence.Iterator(image)]
    if max_frames and len(frames) > max_frames:
        if max_frames == 1:
            frames = [frames[0]]
        else:
            step = (len(frames) - 1) / (max_frames - 1)
            frames = [frames[round(i * step)] for i in range(max_frames)]
    return frames


def frame_luminance(frame):
    rgba = frame.convert("RGBA")
    bg = Image.new("RGBA", rgba.size, (0, 0, 0, 255))
    bg.alpha_composite(rgba)
    return bg.convert("L")


def union_bbox(frames, threshold):
    left = top = 10**9
    right = bottom = -1
    for frame in frames:
        bw = frame_luminance(frame).point(lambda p: 255 if p >= threshold else 0)
        bbox = bw.getbbox()
        if not bbox:
            continue
        l, t, r, b = bbox
        left, top = min(left, l), min(top, t)
        right, bottom = max(right, r), max(bottom, b)
    if right < left or bottom < top:
        return (0, 0, frames[0].width, frames[0].height)
    return (left, top, right, bottom)


def expand_bbox(bbox, image_size, padding, target_aspect):
    img_w, img_h = image_size
    l, t, r, b = bbox
    l -= padding
    t -= padding
    r += padding
    b += padding

    cx = (l + r) / 2
    cy = (t + b) / 2
    width = max(1, r - l)
    height = max(1, b - t)
    current_aspect = width / height
    if current_aspect < target_aspect:
        width = height * target_aspect
    else:
        height = width / target_aspect

    l = round(cx - width / 2)
    r = round(cx + width / 2)
    t = round(cy - height / 2)
    b = round(cy + height / 2)

    if l < 0:
        r -= l
        l = 0
    if t < 0:
        b -= t
        t = 0
    if r > img_w:
        l -= r - img_w
        r = img_w
    if b > img_h:
        t -= b - img_h
        b = img_h

    l, t = max(0, l), max(0, t)
    r, b = min(img_w, r), min(img_h, b)
    return (l, t, r, b)


def resize_contain(gray):
    src_w, src_h = gray.size
    scale = min(W / src_w, H / src_h)
    new_size = (max(1, round(src_w * scale)), max(1, round(src_h * scale)))
    resized = gray.resize(new_size, Image.Resampling.LANCZOS)
    canvas = Image.new("L", (W, H), 0)
    canvas.paste(resized, ((W - new_size[0]) // 2, (H - new_size[1]) // 2))
    return canvas


def adapt_frames(frames, threshold, padding, fit, invert):
    if fit == "auto-crop":
        bbox = union_bbox(frames, threshold)
        crop = expand_bbox(bbox, frames[0].size, padding, W / H)
    else:
        crop = (0, 0, frames[0].width, frames[0].height)

    bw_frames = []
    packed_frames = []
    for frame in frames:
        gray = frame_luminance(frame).crop(crop)
        if fit == "stretch" or fit == "auto-crop":
            canvas = gray.resize((W, H), Image.Resampling.LANCZOS)
        else:
            canvas = resize_contain(gray)
        bw = canvas.point(lambda p: 255 if p >= threshold else 0).convert("1")
        if invert:
            bw = bw.point(lambda p: 0 if p else 255)
        data = pack(bw)
        if len(data) != BYTES_PER_FRAME:
            raise RuntimeError(f"Bad packed frame size: {len(data)}")
        bw_frames.append(bw)
        packed_frames.append(data)
    return bw_frames, packed_frames, crop


def write_json(path, name, frames_bytes, fps):
    payload = {
        "asset": name,
        "trigger": re.sub(r"\\d+$", "", name),
        "label": re.sub(r"\\d+$", "", name).replace("_", " ").title(),
        "width": W,
        "height": H,
        "frameCount": len(frames_bytes),
        "fps": fps,
        "frameDelay": round(1000 / fps),
        "frameSize": BYTES_PER_FRAME,
        "frames": [list(frame) for frame in frames_bytes],
    }
    path.write_text(json.dumps(payload, separators=(",", ":")) + "\n", encoding="utf-8")


def write_sheet(path, bw_frames, columns, scale):
    sample_count = min(len(bw_frames), columns)
    if sample_count == 0:
        return
    if len(bw_frames) == 1:
        idxs = [0]
    else:
        idxs = [round(i * (len(bw_frames) - 1) / (sample_count - 1)) for i in range(sample_count)]
    tile_w, tile_h = W * scale, H * scale
    label_h = 14
    sheet = Image.new("RGB", (sample_count * tile_w, tile_h + label_h), (255, 255, 255))
    draw = ImageDraw.Draw(sheet)
    amber, bg = (247, 182, 90), (8, 10, 13)
    for col, idx in enumerate(idxs):
        big = bw_frames[idx].resize((tile_w, tile_h), Image.Resampling.NEAREST)
        base = Image.new("RGB", big.size, bg)
        lit = Image.new("RGB", big.size, amber)
        tile = Image.composite(lit, base, big)
        x = col * tile_w
        sheet.paste(tile, (x, 0))
        draw.text((x + 3, tile_h + 2), f"f{idx + 1}", fill=(0, 0, 0))
    sheet.save(path)


def main():
    parser = argparse.ArgumentParser(description="Adapt Mochi GIFs for Bobik OLED previews.")
    parser.add_argument("input", help="local GIF, URL, or bare repo GIF name such as happy")
    parser.add_argument("--repo", choices=sorted(RAW_BASES), default="watcher")
    parser.add_argument("--name", help="output base name, default mochi_<input>01")
    parser.add_argument("--out-dir", default="/tmp/bobik-mochi")
    parser.add_argument("--fps", type=int, default=12)
    parser.add_argument("--max-frames", type=int, default=36)
    parser.add_argument("--threshold", type=int, default=48)
    parser.add_argument("--padding", type=int, default=10)
    parser.add_argument("--fit", choices=["auto-crop", "contain", "stretch"], default="auto-crop")
    parser.add_argument("--invert", action="store_true")
    parser.add_argument("--preview-scale", type=int, default=4)
    args = parser.parse_args()

    source = resolve_input(args.input, args.repo)
    stem = slug(args.input)
    name = args.name or f"mochi_{stem}01"
    if not re.fullmatch(r"[a-z][a-z0-9_]*", name):
        sys.exit("--name must be lowercase letters/digits/underscores and start with a letter")

    out_dir = Path(args.out_dir).resolve()
    out_dir.mkdir(parents=True, exist_ok=True)
    cache_dir = Path(tempfile.gettempdir()) / "bobik-mochi-source"
    source_path = fetch_if_needed(source, cache_dir)
    frames = load_frames(source_path, args.max_frames)
    if not frames:
        sys.exit(f"No frames loaded from {source_path}")

    bw_frames, packed_frames, crop = adapt_frames(
        frames=frames,
        threshold=args.threshold,
        padding=args.padding,
        fit=args.fit,
        invert=args.invert,
    )

    header_path = out_dir / f"{name}.h"
    json_path = out_dir / f"{name}.json"
    gif_path = out_dir / f"{name}.gif"
    sheet_path = out_dir / f"{name}_sheet.png"

    header_path.write_text(emit_header(name, packed_frames, args.fps), encoding="utf-8")
    write_json(json_path, name, packed_frames, args.fps)
    build_gif(bw_frames, str(gif_path), args.preview_scale, args.fps)
    write_sheet(sheet_path, bw_frames, columns=8, scale=2)

    print(f"source: {source}")
    print(f"source_cache: {source_path}")
    print(f"frames: {len(packed_frames)} @ {args.fps}fps")
    print(f"fit: {args.fit}, crop: {crop}, threshold: {args.threshold}, padding: {args.padding}")
    print(f"wrote: {header_path}")
    print(f"wrote: {json_path}")
    print(f"wrote: {gif_path}")
    print(f"wrote: {sheet_path}")
    print("note: keep third-party Mochi source/derived assets local unless licensing is cleared.")


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""
Export firmware OLED face headers to app/public/animations/*.json.

The preview page reads manifest.json, so new generated faces become visible
there after running this script.
"""
import json
import os
import re
import sys

W = 128
H = 64
BYTES_PER_FRAME = W * H // 8
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(SCRIPT_DIR)
FIRMWARE_SRC = os.path.join(REPO_ROOT, "firmware", "src")
OUT_DIR = os.path.join(REPO_ROOT, "app", "public", "animations")

TRIGGER_OVERRIDES = {
    "angry01": ("paused", "Angry / Paused"),
    "relax01": ("break", "Break / Relax"),
}

LABEL_OVERRIDES = {
    "coffee01": "Coffee",
    "focus01": "Focus",
    "idle01": "Idle",
    "love01": "Love",
    "startup01": "Startup",
    "sweat01": "Sweat",
}


def strip_digits(name):
    return re.sub(r"\d+$", "", name)


def label_for(name):
    if name in LABEL_OVERRIDES:
        return LABEL_OVERRIDES[name]
    return strip_digits(name).replace("_", " ").title()


def parse_header(path):
    with open(path, encoding="utf-8") as f:
        text = f.read()

    name = os.path.splitext(os.path.basename(path))[0]
    macro = name.upper()

    count_match = re.search(rf"#define\s+{macro}_FRAME_COUNT\s+(\d+)", text)
    fps_match = re.search(rf"#define\s+{macro}_FPS\s+(\d+)", text)
    delay_match = re.search(rf"#define\s+{macro}_FRAME_DELAY\s+(\d+)", text)
    frames = []

    for match in re.finditer(
        rf"const\s+unsigned\s+char\s+PROGMEM\s+{re.escape(name)}_frame_\d+\[\]\s*=\s*\{{(.*?)\}};",
        text,
        re.DOTALL,
    ):
        values = [int(v, 16) for v in re.findall(r"0x[0-9a-fA-F]{2}", match.group(1))]
        if len(values) != BYTES_PER_FRAME:
            raise ValueError(f"{path}: frame has {len(values)} bytes, expected {BYTES_PER_FRAME}")
        frames.append(values)

    if not frames:
        return None

    frame_count = int(count_match.group(1)) if count_match else len(frames)
    if frame_count != len(frames):
        raise ValueError(f"{path}: macro says {frame_count} frames, parsed {len(frames)}")

    fps = int(fps_match.group(1)) if fps_match else 12
    frame_delay = int(delay_match.group(1)) if delay_match else round(1000 / fps)
    trigger, label = TRIGGER_OVERRIDES.get(name, (strip_digits(name), label_for(name)))

    return {
        "asset": name,
        "trigger": trigger,
        "label": label,
        "width": W,
        "height": H,
        "frameCount": frame_count,
        "fps": fps,
        "frameDelay": frame_delay,
        "frameSize": BYTES_PER_FRAME,
        "frames": frames,
    }


def main():
    os.makedirs(OUT_DIR, exist_ok=True)
    animations = []

    for filename in sorted(os.listdir(FIRMWARE_SRC)):
        if not filename.endswith(".h"):
            continue
        if filename == "angry_bitmap.h":
            continue
        path = os.path.join(FIRMWARE_SRC, filename)
        parsed = parse_header(path)
        if not parsed:
            continue

        asset = parsed["asset"]
        out_path = os.path.join(OUT_DIR, asset + ".json")
        with open(out_path, "w", encoding="utf-8") as f:
            json.dump(parsed, f, separators=(",", ":"))
            f.write("\n")

        animations.append({
            "value": parsed["trigger"],
            "label": parsed["label"],
            "asset": asset,
            "kind": "bitmap",
            "frameCount": parsed["frameCount"],
            "fps": parsed["fps"],
        })
        print(f"exported {asset}.json ({parsed['frameCount']} frames)")

    manifest = {
        "generatedBy": "tools/export_oled_animations.py",
        "animations": animations,
    }
    with open(os.path.join(OUT_DIR, "manifest.json"), "w", encoding="utf-8") as f:
        json.dump(manifest, f, indent=2)
        f.write("\n")

    print(f"wrote manifest.json with {len(animations)} animations")
    return 0


if __name__ == "__main__":
    sys.exit(main())

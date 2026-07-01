#!/usr/bin/env python3
"""
wire_face.py — wire a generated face header into firmware/src/main.cpp so the
firmware can render and trigger it. Idempotent: run it as many times as you like;
already-wired faces are left untouched.

USAGE
  python3 wire_face.py NAME [--trigger WORD] [--check] [--dry-run]

  NAME       the header base name (e.g. sweat01 -> firmware/src/sweat01.h)
  --trigger  the animation word used in the API/MQTT payload
             (default: NAME with trailing digits stripped, e.g. sweat01 -> sweat)
  --check    report what is / isn't wired and exit non-zero if incomplete;
             changes nothing
  --dry-run  print the edits that WOULD be made; changes nothing

This performs the 4 edits make_face.py describes in its wiring hints:
  1. #include "NAME.h"
  2. void drawXxxAnimation();            (prototype)
  3. void drawXxxAnimation() { ... }     (playback function)
  4. else-if branch in updateDisplay()   (dispatch on the trigger word)
"""
import argparse, os, re, sys

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(SCRIPT_DIR)
MAIN_CPP = os.path.join(REPO_ROOT, "firmware", "src", "main.cpp")


def draw_fn_body(name, fn, NAME):
    return f"""void {fn}() {{
  static int frame = 0;
  static unsigned long lastFrameTime = 0;
  static unsigned long lastStart = 0;

  // Reset on animation start
  if (animationStartTime != lastStart) {{
    frame = 0;
    lastFrameTime = 0;
    lastStart = animationStartTime;
  }}

  unsigned long now = millis();
  if (now - lastFrameTime < {NAME}_FRAME_DELAY) return;
  lastFrameTime = now;

  // Draw frame
  display.clearBuffer();
  const uint8_t* frameData = (const uint8_t*)pgm_read_ptr(&{name}_frames[frame]);
  display.drawBitmap(0, 0, 128 / 8, 64, frameData);
  display.sendBuffer();

  frame++;
  if (frame >= {NAME}_FRAME_COUNT) frame = 0;
}}

"""


def insert_after_last(text, line_re, block, label):
    """Insert `block` right after the last line matching line_re. Return (text, note)."""
    matches = list(re.finditer(line_re, text, re.MULTILINE))
    if not matches:
        sys.exit(f"ERROR: could not find an anchor for {label} "
                 f"(pattern: {line_re!r}). main.cpp layout changed — wire it by hand.")
    end = matches[-1].end()
    return text[:end] + "\n" + block.rstrip("\n") + text[end:], f"added {label}"


def insert_before(text, anchor_re, block, label):
    m = re.search(anchor_re, text, re.MULTILINE)
    if not m:
        sys.exit(f"ERROR: could not find an anchor for {label} "
                 f"(pattern: {anchor_re!r}). main.cpp layout changed — wire it by hand.")
    at = m.start()
    return text[:at] + block + text[at:], f"added {label}"


def main():
    ap = argparse.ArgumentParser(description="Wire a face header into main.cpp")
    ap.add_argument("name")
    ap.add_argument("--trigger")
    ap.add_argument("--check", action="store_true")
    ap.add_argument("--dry-run", action="store_true")
    a = ap.parse_args()

    if not re.fullmatch(r"[a-z][a-z0-9_]*", a.name):
        sys.exit("NAME must be lowercase letters/digits, e.g. sweat01")

    name = a.name
    NAME = name.upper()                      # macro prefix, matches the header (sweat01 -> SWEAT01)
    base = re.sub(r"\d+$", "", name)         # function/trigger base (sweat01 -> sweat)
    fn = "draw" + base[0].upper() + base[1:] + "Animation"   # matches repo convention: drawSweatAnimation
    trigger = a.trigger or base

    header = os.path.join(REPO_ROOT, "firmware", "src", name + ".h")
    if not os.path.exists(header):
        sys.exit(f"ERROR: {header} not found. Generate it first with make_face.py.")
    if not os.path.exists(MAIN_CPP):
        sys.exit(f"ERROR: {MAIN_CPP} not found.")

    with open(MAIN_CPP) as f:
        text = f.read()

    # What is already present?
    have_include = f'#include "{name}.h"' in text
    have_proto = re.search(rf"^void {fn}\(\);", text, re.MULTILINE) is not None
    have_fn = re.search(rf"^void {fn}\(\) \{{", text, re.MULTILINE) is not None
    have_branch = f'currentAnimation == "{trigger}"' in text

    status = {
        "include": have_include,
        "prototype": have_proto,
        "draw function": have_fn,
        f'dispatch (trigger "{trigger}")': have_branch,
    }

    if a.check:
        print(f"wire status for {name} (trigger \"{trigger}\"):")
        for k, v in status.items():
            print(f"  [{'x' if v else ' '}] {k}")
        missing = [k for k, v in status.items() if not v]
        if missing:
            print(f"MISSING: {', '.join(missing)}")
            sys.exit(1)
        print("fully wired ✓")
        return

    notes = []
    if not have_include:
        text, note = insert_after_last(
            text, r'^#include "[a-z0-9_]+\.h".*$',
            f'#include "{name}.h"          // face: {trigger}', "include")
        notes.append(note)
    if not have_proto:
        text, note = insert_after_last(
            text, r"^void draw\w+Animation\(\);.*$",
            f"void {fn}();", "prototype")
        notes.append(note)
    if not have_fn:
        text, note = insert_before(
            text, r"^void updateDisplay\(\) \{",
            draw_fn_body(name, fn, NAME), "draw function")
        notes.append(note)
    if not have_branch:
        branch = (f'  }} else if (currentAnimation == "{trigger}") {{\n'
                  f'    {fn}();\n')
        text, note = insert_before(
            text, r'^  \} else if \(currentAnimation == "pomodoro"\) \{',
            branch, f'dispatch branch (trigger "{trigger}")')
        notes.append(note)

    if not notes:
        print(f"{name} is already fully wired (trigger \"{trigger}\") — nothing to do.")
        return

    if a.dry_run:
        print(f"[dry-run] would edit {MAIN_CPP}:")
        for n in notes:
            print(f"  - {n}")
        return

    with open(MAIN_CPP, "w") as f:
        f.write(text)
    print(f"Wired {name} into main.cpp (trigger \"{trigger}\"):")
    for n in notes:
        print(f"  - {n}")
    print("\nNext:  tools/flash_face.sh --trigger " + trigger)


if __name__ == "__main__":
    main()

# Mochi Reference Animations

This project can use Mochi-style GIFs as local reference material when designing
Bobik faces. Do not commit downloaded Mochi GIFs or derived headers unless the
asset license is cleared.

## Sources Checked

| Source | Useful for | Notes |
| --- | --- | --- |
| <https://github.com/pham-tuan-binh/watcher-mochi> | Full reference set | `sd_content/` has 63 GIFs. The repo states the GIF animations belong to Dasai and are included for personal use. |
| <https://github.com/huykhoong/gif2cpp> | Conversion approach | Tooling reference for GIF to C/C++ arrays for monochrome OLED displays. |
| <https://github.com/tamdilip/emote-buddy> | Web playback example | Has a cached Mochi-style `smile.gif`, not a broad face library. |
| <https://github.com/upiir/esp32s3_oled_dasai_mochi> | ESP32/OLED workflow | Good example project with Rive/PNG/GIF assets and OLED conversion links. |

## Local Adaptation

Use `tools/adapt_mochi_gif.py` to create local preview assets without touching
firmware:

```bash
tools/adapt_mochi_gif.py happy --repo watcher --out-dir /tmp/bobik-mochi
```

Output files:

- `/tmp/bobik-mochi/mochi_happy01.h`
- `/tmp/bobik-mochi/mochi_happy01.json`
- `/tmp/bobik-mochi/mochi_happy01.gif`
- `/tmp/bobik-mochi/mochi_happy01_sheet.png`

The script accepts:

```bash
# watcher-mochi sd_content/<name>.gif
tools/adapt_mochi_gif.py angry --repo watcher --out-dir /tmp/bobik-mochi

# emote-buddy assets/gifs/<name>.gif
tools/adapt_mochi_gif.py smile --repo emote-buddy --out-dir /tmp/bobik-mochi

# upiir repo root GIF
tools/adapt_mochi_gif.py RIVE_big_smile_animation.gif --repo upiir --out-dir /tmp/bobik-mochi

# arbitrary local file or URL
tools/adapt_mochi_gif.py /path/to/source.gif --name local_face01 --out-dir /tmp/bobik-mochi
```

Useful tuning options:

```bash
--max-frames 24
--fps 12
--threshold 48
--padding 10
--fit auto-crop     # default; other values: contain, stretch
--invert
```

Only move a generated `.h` into `firmware/src/` after the local preview looks
good and the asset can be used in the project.

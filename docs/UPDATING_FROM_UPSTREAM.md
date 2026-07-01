# Keeping this fork in sync with the original Tabbie

This repo is **your fork**. `origin` is your copy (`mdavydau/bobik`); the original
project is added as `upstream` (`lloyd-december/tabbie`). You develop freely on
`origin`, and pull the original's updates only when you choose to.

## Remotes

```bash
git remote -v
# origin    https://github.com/mdavydau/bobik.git       (your fork - push here)
# upstream  https://github.com/lloyd-december/tabbie.git (original - read only)
```

## See what changed upstream (without touching your code)

```bash
git fetch upstream
git log --oneline main..upstream/main      # commits they have that you don't
git diff main..upstream/main               # the actual changes
```

## Pull upstream changes in

```bash
git checkout main
git fetch upstream
git merge upstream/main          # or: git rebase upstream/main
# resolve any conflicts (likely in firmware/src/main.cpp where you added the
# animated angry face + MQTT bridge), then:
git push origin main
```

Prefer trying updates on a branch first so `main` stays safe:

```bash
git checkout -b try-upstream
git merge upstream/main          # test/build; if you like it, merge to main
```

## Your changes vs. upstream

Your additions live alongside upstream's code:
- `firmware/src/angry01.h`, `mqtt_config.example.h` — new files (never conflict)
- `tools/` — your scripts (never conflict)
- `docs/REMOTE_CONTROL.md`, `docs/MQTT_BRIDGE.md`, this file — new docs
- edits to `firmware/src/main.cpp` and `platformio.ini` — the only places an
  upstream update might conflict; resolve by keeping both their change and your
  animated-angry / MQTT additions.

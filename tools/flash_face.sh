#!/usr/bin/env bash
# flash_face.sh — build the Tabbie firmware and flash it over USB, predictably.
# The one command to run after adding/updating a face (see wire_face.py).
#
# USAGE
#   tools/flash_face.sh [--port DEV] [--trigger WORD] [--host H] [--no-upload]
#
#   --port DEV      serial port (default: auto-detect a USB-serial device,
#                   or $PORT). On macOS that's /dev/cu.usbserial-* etc.
#   --trigger WORD  after flashing, wait for reboot then set this face via HTTP
#                   (uses tabbie-ctl.sh; needs the board on WiFi)
#   --host H        Tabbie host for --trigger (default $TABBIE_HOST or tabbie.local)
#   --no-upload     build only, do not flash (useful to catch compile errors)
#
# EXAMPLES
#   tools/flash_face.sh                         # build + flash, auto port
#   tools/flash_face.sh --trigger sweat         # build + flash + show the face
#   tools/flash_face.sh --port /dev/cu.usbserial-0001 --trigger sweat
set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(dirname "$SCRIPT_DIR")"
FW_DIR="$REPO_ROOT/firmware"

PORT="${PORT:-}"
TRIGGER=""
HOST="${TABBIE_HOST:-tabbie.local}"
UPLOAD=true

while [ $# -gt 0 ]; do
  case "$1" in
    --port)     PORT="$2"; shift 2 ;;
    --trigger)  TRIGGER="$2"; shift 2 ;;
    --host)     HOST="$2"; shift 2 ;;
    --no-upload) UPLOAD=false; shift ;;
    -h|--help)  sed -n '2,25p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) echo "unknown arg: $1" >&2; exit 1 ;;
  esac
done

command -v pio >/dev/null 2>&1 || {
  echo "ERROR: 'pio' (PlatformIO) not on PATH." >&2
  echo "  install: https://platformio.org/install/cli  (or add ~/.platformio/penv/bin to PATH)" >&2
  exit 1
}

autodetect_port() {
  # first matching USB-serial device (common ESP32 USB-UART chips)
  for p in /dev/cu.usbserial-* /dev/cu.SLAB_USBtoUART* /dev/cu.wchusbserial* \
           /dev/ttyUSB* /dev/ttyACM*; do
    [ -e "$p" ] && { echo "$p"; return 0; }
  done
  return 1
}

echo "==> Building firmware ($FW_DIR)"
if ! pio run -d "$FW_DIR"; then
  echo "ERROR: build failed — not flashing." >&2
  exit 1
fi

if ! $UPLOAD; then
  echo "==> Build OK (skipping upload, --no-upload)"
  exit 0
fi

if [ -z "$PORT" ]; then
  PORT="$(autodetect_port)" || {
    echo "ERROR: no USB-serial port found. Plug in Tabbie, or pass --port DEV." >&2
    echo "  visible ports:"; ls /dev/cu.* 2>/dev/null | sed 's/^/    /'
    exit 1
  }
  echo "==> Auto-detected port: $PORT"
fi

echo "==> Flashing over USB ($PORT)"
if ! pio run -d "$FW_DIR" --target upload --upload-port "$PORT"; then
  echo "ERROR: upload failed. Is Tabbie the only board plugged in? Try --port." >&2
  exit 1
fi
echo "==> Flash complete."

if [ -n "$TRIGGER" ]; then
  echo "==> Waiting ~8s for reboot, then triggering '$TRIGGER' on $HOST"
  sleep 8
  TABBIE_HOST="$HOST" "$SCRIPT_DIR/tabbie-ctl.sh" "$TRIGGER" "flash_face" \
    || echo "WARN: could not trigger '$TRIGGER' (board may still be booting / off WiFi)."
fi

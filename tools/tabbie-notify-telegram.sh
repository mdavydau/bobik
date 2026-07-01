#!/usr/bin/env bash
# Forward Bobik face-change events from MQTT to Telegram.
#
# Intended server usage:
#   source .mqtt.env
#   source .telegram.env
#   tools/tabbie-notify-telegram.sh cron
#
# Required Telegram env:
#   TELEGRAM_BOT_TOKEN
#   TELEGRAM_CHAT_ID
#   TELEGRAM_API_BASE               optional, default https://api.telegram.org/bot
#
# MQTT env:
#   MQTT_HOST (default localhost)   MQTT_PORT (default 1883)
#   MQTT_USER / MQTT_PASS           optional broker auth
#   MQTT_NOTIFY_TOPIC               default tabbie/notify
set -u -o pipefail

mode="${1:-listen}"
HOST="${MQTT_HOST:-localhost}"
PORT="${MQTT_PORT:-1883}"
TOPIC="${MQTT_NOTIFY_TOPIC:-tabbie/notify}"
WINDOW_SECONDS="${WINDOW_SECONDS:-58}"
TELEGRAM_API_BASE="${TELEGRAM_API_BASE:-https://api.telegram.org/bot}"

usage() {
  sed -n '2,17p' "$0" | sed 's/^# \{0,1\}//'
  cat <<'EOF'

Modes:
  listen    keep listening forever
  cron      listen for WINDOW_SECONDS (default 58), suitable for minutely cron
  once      forward one event, then exit
EOF
}

case "$mode" in
  -h|--help|help) usage; exit 0 ;;
  listen|cron|once) ;;
  *) echo "usage: $0 [listen|cron|once]" >&2; exit 2 ;;
esac

require_cmd() {
  command -v "$1" >/dev/null 2>&1 || {
    echo "ERROR: missing required command: $1" >&2
    exit 1
  }
}

require_cmd curl
require_cmd mosquitto_sub
require_cmd python3

[ -n "${TELEGRAM_BOT_TOKEN:-}" ] || {
  echo "ERROR: TELEGRAM_BOT_TOKEN is not set" >&2
  exit 1
}
[ -n "${TELEGRAM_CHAT_ID:-}" ] || {
  echo "ERROR: TELEGRAM_CHAT_ID is not set" >&2
  exit 1
}

auth=()
[ -n "${MQTT_USER:-}" ] && auth+=(-u "$MQTT_USER")
[ -n "${MQTT_PASS:-}" ] && auth+=(-P "$MQTT_PASS")

sub_args=(-h "$HOST" -p "$PORT" "${auth[@]}" -t "$TOPIC")
case "$mode" in
  cron) sub_args+=(-W "$WINDOW_SECONDS") ;;
  once) sub_args+=(-C 1 -W "$WINDOW_SECONDS") ;;
esac

format_message() {
  python3 - "$1" <<'PY'
import json
import sys

raw = sys.argv[1]
emoji = {
    "coffee": "coffee",
    "status_alert": "status",
    "sweat": "warning",
    "paused": "paused",
    "sleepy": "sleepy",
    "mochi_happy": "morning",
    "idle": "idle",
}

try:
    data = json.loads(raw)
except json.JSONDecodeError:
    print(f"Bobik face change\n{raw}")
    raise SystemExit

anim = data.get("animation") or data.get("anim") or "unknown"
task = data.get("task") or ""
when = data.get("time") or ""
marker = emoji.get(anim, "face")

lines = [f"Bobik: {marker} -> {anim}"]
if task:
    lines.append(f"Task: {task}")
if when:
    lines.append(f"Time: {when}")
print("\n".join(lines))
PY
}

send_telegram() {
  local payload="$1"
  local text

  text="$(format_message "$payload")"
  curl -fsS -m 10 --retry 2 -X POST \
    "${TELEGRAM_API_BASE}${TELEGRAM_BOT_TOKEN}/sendMessage" \
    -d "chat_id=${TELEGRAM_CHAT_ID}" \
    --data-urlencode "text=${text}" >/dev/null
}

mosquitto_sub "${sub_args[@]}" | while IFS= read -r payload; do
  [ -n "$payload" ] || continue
  if ! send_telegram "$payload"; then
    echo "ERROR: failed to send Telegram message for payload: $payload" >&2
  fi
done

sub_status=${PIPESTATUS[0]}
case "$sub_status" in
  0|27) exit 0 ;; # 27 is mosquitto timeout with -W on common builds.
  *) echo "ERROR: mosquitto_sub failed with status $sub_status" >&2; exit "$sub_status" ;;
esac

#!/usr/bin/env bash
# Tabbie after-hours mood:
#   If you're working after 18:00, show the ANGRY face ("go rest!").
#   If you've stepped away in the evening, calm it back to idle.
#   During the day it does nothing, so the dashboard/pomodoro stay in control.
#
# Meant to run periodically (cron every ~5 min). Tunables via env vars:
#   TABBIE_HOST         default tabbie.local   (or set an IP)
#   TABBIE_ANGRY_START  default 18             (evening cutoff hour, 24h)
set -uo pipefail

HOST="${TABBIE_HOST:-tabbie.local}"
START_HOUR="${TABBIE_ANGRY_START:-18}"
ACTIVE_IDLE=120     # < this many seconds idle  => "working"
AWAY_IDLE=600       # >= this many seconds idle => "stepped away"

hour=$((10#$(date +%H)))
idle=$(ioreg -c IOHIDSystem 2>/dev/null | awk '/HIDIdleTime/{print int($NF/1000000000); exit}')
[ -z "${idle:-}" ] && idle=0

send() {
  curl -s -m 5 -X POST "http://$HOST/api/animation" \
    -H "Content-Type: application/json" \
    -d "{\"animation\":\"$1\",\"task\":\"$2\"}" >/dev/null 2>&1
}

ts="$(date '+%F %T')"
if [ "$hour" -ge "$START_HOUR" ]; then
  if [ "$idle" -lt "$ACTIVE_IDLE" ]; then
    send paused "working after ${START_HOUR}:00 - go rest"
    echo "$ts  angry  (after-hours, active, idle=${idle}s)"
  elif [ "$idle" -ge "$AWAY_IDLE" ]; then
    send idle "evening, away"
    echo "$ts  idle   (after-hours, away, idle=${idle}s)"
  else
    echo "$ts  hold   (after-hours, idle=${idle}s)"
  fi
else
  echo "$ts  skip   (before ${START_HOUR}:00)"
fi

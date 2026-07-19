#!/usr/bin/env bash
# tabbie-meter — control the Bobik token-meter loop and switch its display mode.
#
#   tabbie-meter face      # animated BMO mood face (+ rate arrow / load bars)
#   tabbie-meter stats     # live numeric statistics screen (no face)
#   tabbie-meter week      # push a one-off week (Mon–Sun) snapshot (holds ~30s)
#   tabbie-meter stop      # stop the loop
#   tabbie-meter status    # is it running, in which mode, last push
#   tabbie-meter restart   # restart in the last used mode
#
# The loop is detached (nohup) so it keeps running after you close the terminal.
# Broker / tuning come from tools/tokenmeter.env (see tokenmeter.env.example).
set -uo pipefail

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LOOP="$DIR/tabbie-tokenmeter.sh"
LOG="/tmp/tabbie-meter.log"
MODEF="/tmp/tabbie-meter.mode"

stop() { pkill -f 'tabbie-tokenmeter.sh' 2>/dev/null; rm -f /tmp/tabbie-meter.hold; }

start() {   # $1 = mode label (face|stats) -> MOOD_FACE 1|0
  local mf=1; [ "$1" = "stats" ] && mf=0
  stop; sleep 1
  echo "$1" > "$MODEF"
  MOOD_FACE="$mf" nohup "$LOOP" >"$LOG" 2>&1 &
  disown 2>/dev/null || true
  sleep 2
  if pgrep -f 'tabbie-tokenmeter.sh' >/dev/null; then
    echo "✅ meter → $1"; tail -1 "$LOG" 2>/dev/null
  else
    echo "❌ failed to start; see $LOG" >&2; tail -3 "$LOG" 2>/dev/null; exit 1
  fi
}

case "${1:-status}" in
  face)    start face ;;
  stats)   start stats ;;
  restart) start "$(cat "$MODEF" 2>/dev/null || echo face)" ;;
  week)    "$LOOP" week ;;
  stop)    stop; echo "meter stopped" ;;
  status)
    if pgrep -f 'tabbie-tokenmeter.sh' >/dev/null; then
      echo "running (mode: $(cat "$MODEF" 2>/dev/null || echo '?'))"
      tail -1 "$LOG" 2>/dev/null
    else
      echo "stopped"
    fi ;;
  *) echo "usage: $(basename "$0") {face|stats|week|stop|restart|status}" >&2; exit 2 ;;
esac

#!/usr/bin/env bash
# Tabbie expression demo — cycles through every face with a fixed delay.
#
# Usage:
#   ./demo_expressions.sh [host] [delay_seconds] [--loop]
#
# Examples:
#   ./demo_expressions.sh                       # tabbie.local, 2s each, one pass
#   ./demo_expressions.sh tabbie.local 2        # explicit host + delay
#   ./demo_expressions.sh 192.168.233.229 1.5   # by IP, faster
#   ./demo_expressions.sh tabbie.local 2 --loop # repeat until Ctrl-C
set -uo pipefail

HOST="${1:-tabbie.local}"
DELAY="${2:-2}"
LOOP=false
[[ "${3:-}" == "--loop" ]] && LOOP=true

# Every animation the firmware knows how to render (see firmware/src/main.cpp updateDisplay)
EXPRESSIONS=(startup idle focus pomodoro break love complete paused)

send() {
  if curl -s -m 5 -X POST "http://$HOST/api/animation" \
        -H "Content-Type: application/json" \
        -d "{\"animation\":\"$1\",\"task\":\"demo\"}" >/dev/null 2>&1; then
    echo "  ✓ $1"
  else
    echo "  ✗ $1  (no response — is Tabbie on and reachable at $HOST?)"
  fi
}

# fail fast if the device isn't reachable
if ! curl -s -m 5 "http://$HOST/api/status" >/dev/null 2>&1; then
  echo "Cannot reach Tabbie at http://$HOST — check it's powered and on WiFi." >&2
  exit 1
fi

echo "Tabbie expression demo → $HOST  (${DELAY}s each)$([[ $LOOP == true ]] && echo '  [looping]')"
while :; do
  for e in "${EXPRESSIONS[@]}"; do
    send "$e"
    sleep "$DELAY"
  done
  $LOOP || break
done

send idle
echo "Done — back to idle."

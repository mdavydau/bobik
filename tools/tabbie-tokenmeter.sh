#!/usr/bin/env bash
# tabbie-tokenmeter — push a live "context token meter" to Bobik's OLED.
#
# Every INTERVAL seconds it reads the peak context tokens of the most-recent
# Claude Code session from `agentsview` and publishes them to the Bobik MQTT
# command topic as {"meter":{"used":N,"limit":M,"label":"CTX"}}. The board
# renders a full-screen gauge (big number in thousands + % + progress bar).
#
# The LIMIT is your "too much" red line — default is a past bloated session
# (~85k). Above it the bar blinks full.
#
#   ./tabbie-tokenmeter.sh                 # loop forever, 5s cadence
#   INTERVAL=30 ./tabbie-tokenmeter.sh     # slower refresh
#   METER_LIMIT=120000 ./tabbie-tokenmeter.sh
#   SESSION_ID=<uuid> ./tabbie-tokenmeter.sh   # pin a specific session
#   ONCE=1 ./tabbie-tokenmeter.sh          # publish one sample and exit (test)
#
# Requires: agentsview, jq, mosquitto_pub  (brew install mosquitto jq)
# MQTT config via env (same names as tabbie-pub.sh):
#   MQTT_HOST (default localhost)  MQTT_PORT (1883)  MQTT_TOPIC (tabbie/cmd)
#   MQTT_USER / MQTT_PASS (optional broker auth)
set -uo pipefail

# Load the local publisher config if present (broker host/creds live here, not
# in the board's mqtt_config.h). Override path with TOKENMETER_ENV.
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CONFIG="${TOKENMETER_ENV:-$SCRIPT_DIR/tokenmeter.env}"
# shellcheck disable=SC1090
[ -f "$CONFIG" ] && . "$CONFIG"

INTERVAL="${INTERVAL:-5}"
METRIC="${METRIC:-day}"                  # "day" = today's output tokens (grows all
                                        # day); "peak" = current session context peak
METER_LIMIT="${METER_LIMIT:-200000}"    # the "too much" red line for the metric
METER_LABEL="${METER_LABEL:-DAY}"
AGENT="${AGENT:-claude}"

HOST="${MQTT_HOST:-localhost}"
PORT="${MQTT_PORT:-1883}"
TOPIC="${MQTT_TOPIC:-tabbie/cmd}"

for bin in agentsview jq mosquitto_pub; do
  command -v "$bin" >/dev/null || { echo "missing: $bin" >&2; exit 1; }
done

pub_args=(-h "$HOST" -p "$PORT" -t "$TOPIC")
[ -n "${MQTT_USER:-}" ] && pub_args+=(-u "$MQTT_USER")
[ -n "${MQTT_PASS:-}" ] && pub_args+=(-P "$MQTT_PASS")

# Most-recent session id for AGENT (unless pinned via SESSION_ID).
current_session() {
  [ -n "${SESSION_ID:-}" ] && { echo "$SESSION_ID"; return; }
  agentsview session list --agent "$AGENT" --limit 1 2>/dev/null \
    | awk '/[0-9a-f]{8}-[0-9a-f]{4}-/{print $1; exit}'
}

# Peak context tokens for a session (JSON via the deprecated-but-machine-readable
# token-use; strip agentsview's log preamble before the first "{").
peak_tokens() {
  agentsview token-use "$1" 2>/dev/null | sed -n '/^{/,$p' \
    | jq -r '.peak_context_tokens // 0'
}

# Today's cumulative output tokens across ALL sessions (sum of 5-min buckets from
# the local-day activity report). Grows through the day; resets at midnight.
today_output_tokens() {
  agentsview activity report --preset day --json 2>/dev/null \
    | jq -r '[.buckets[].output_tokens] | add // 0'
}

# Output tokens of a given day (YYYY-MM-DD), all agents.
day_output_tokens() {
  agentsview activity report --preset day --date "$1" --json 2>/dev/null \
    | jq -r '[.buckets[].output_tokens] | add // 0'
}

# Today's output tokens for one agent (claude/codex/...). Uses --no-sync: call
# today_output_tokens() first each tick to warm the DB, else rapid calls race.
model_today() {
  local n; n="$(agentsview activity report --preset day --agent "$1" --no-sync --json 2>/dev/null \
    | jq -r '[.buckets[].output_tokens] | add // 0')"
  [ -n "$n" ] && [ "$n" != "null" ] && echo "$n" || echo 0
}

# Most recent day BEFORE today that had any activity → "TOKENS<TAB>Weekday".
# "Yesterday" is often 0 (weekend), so we walk back until we find a worked day.
prev_active_day() {
  local d tok i
  # C-style loop so it doesn't depend on IFS (the caller sets IFS=$'\t').
  for ((i=1; i<=10; i++)); do
    d="$(date -v-"${i}"d +%Y-%m-%d)"
    tok="$(day_output_tokens "$d")"
    if [ "${tok:-0}" -gt 0 ] 2>/dev/null; then
      printf '%s\t%s' "$tok" "$(date -v-"${i}"d +%a)"
      return
    fi
  done
  printf '0\t'
}

# Previous active day is stable within a run — compute it once (day metric only).
PREV_TOK=0; PREV_LAB=""
if [ "$METRIC" != "peak" ]; then
  IFS=$'\t' read -r PREV_TOK PREV_LAB < <(prev_active_day)
  [ -z "$PREV_TOK" ] && PREV_TOK=0
fi

publish_once() {
  local used tag payload cl=0 cx=0
  if [ "$METRIC" = "peak" ]; then
    local sid; sid="$(current_session)"
    [ -z "$sid" ] && { echo "$(date +%T) no session found" >&2; return 1; }
    used="$(peak_tokens "$sid")"; tag="ctx ${sid:0:8}"
  else
    used="$(today_output_tokens)"          # warms the DB (no --no-sync)
    cl="$(model_today claude)"; cx="$(model_today codex)"
    tag="today CL$cl CX$cx (prev ${PREV_LAB:-?} ${PREV_TOK})"
  fi
  { [ -z "$used" ] || [ "$used" = "null" ]; } && used=0
  payload="$(jq -cn --argjson u "$used" --argjson l "$METER_LIMIT" --arg lab "$METER_LABEL" \
    --argjson p "${PREV_TOK:-0}" --arg pl "${PREV_LAB:-}" \
    --argjson cl "${cl:-0}" --argjson cx "${cx:-0}" \
    '{meter:{used:$u,limit:$l,label:$lab,prev:$p,prevlab:$pl,cl:$cl,cx:$cx}}')"
  mosquitto_pub "${pub_args[@]}" -m "$payload" \
    && echo "$(date +%T) → $payload  ($tag)"
}

if [ "${ONCE:-0}" = "1" ]; then
  publish_once
  exit $?
fi

echo "tabbie-tokenmeter: every ${INTERVAL}s → ${HOST}:${PORT} ${TOPIC} (limit ${METER_LIMIT})"
while true; do
  publish_once
  sleep "$INTERVAL"
done

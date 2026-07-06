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
MOOD_FACE="${MOOD_FACE:-1}"             # 1 = drive a mood face (relax→panic) by load
                                        # with a bottom bar; 0 = numeric meter only
METER_LIMIT="${METER_LIMIT:-200000}"    # the "too much" red line for the metric
METER_LABEL="${METER_LABEL:-DAY}"
AGENT="${AGENT:-claude}"

# Week view (Mon–Sun), pushed on request via `tabbie-tokenmeter.sh week`.
WEEK_LIMIT="${WEEK_LIMIT:-5000000}"   # "too much" red line for a full week
WEEK_HOLD="${WEEK_HOLD:-30}"          # seconds the week view holds before the
                                      # DAY loop is allowed to overwrite it
HOLD_FILE="${HOLD_FILE:-/tmp/tabbie-meter.hold}"

# Total tokens (output+input+cache, both agents) for the "output/total" view.
# Parsing logs is heavy, so cache the result and only recompute every TOTAL_TTL.
SHOW_TOTAL="${SHOW_TOTAL:-1}"          # 0 = plain output/limit view (no total)
TOTAL_TTL="${TOTAL_TTL:-60}"
TOTAL_CACHE="${TOTAL_CACHE:-/tmp/tabbie-total.cache}"

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

# Total tokens today (output + input + cache) across claude + codex, from raw
# logs — the same basis OpenUsage uses. Heavy, so call via cached_total().
total_today() {
  local today_utc today_local ct cx
  today_utc="$(date -u +%Y-%m-%d)"; today_local="$(date +%Y/%m/%d)"
  ct="$(find "$HOME/.claude/projects" -name '*.jsonl' -mtime -1 2>/dev/null | while read -r f; do
    jq -r --arg d "$today_utc" 'select((.timestamp//"")|startswith($d)) | .message.usage // empty
      | ((.input_tokens//0)+(.output_tokens//0)+(.cache_creation_input_tokens//0)+(.cache_read_input_tokens//0))' "$f" 2>/dev/null
  done | awk '{s+=$1} END{print s+0}')"
  cx="$(find "$HOME/.codex/sessions/$today_local" -name 'rollout-*.jsonl' 2>/dev/null | while read -r f; do
    grep -o '"total_tokens":[0-9]*' "$f" 2>/dev/null | grep -o '[0-9]*' | sort -n | tail -1
  done | awk '{s+=$1} END{print s+0}')"
  echo $(( ${ct:-0} + ${cx:-0} ))
}
# Cached wrapper: recompute at most once per TOTAL_TTL seconds.
cached_total() {
  local now age
  now="$(date +%s)"
  if [ -f "$TOTAL_CACHE" ]; then
    age=$(( now - $(stat -f %m "$TOTAL_CACHE" 2>/dev/null || echo 0) ))
    if [ "$age" -lt "$TOTAL_TTL" ]; then cat "$TOTAL_CACHE"; return; fi
  fi
  total_today | tee "$TOTAL_CACHE"
}

# --- Week (Mon–Sun) view, pushed on request ---
# This week's output tokens to-date; optional $1 = agent (else all agents).
week_tokens() {
  agentsview activity report --preset week ${1:+--agent "$1"} --no-sync --json 2>/dev/null \
    | jq -r '[.buckets[].output_tokens] | add // 0'
}
# Previous Mon–Sun week total.
prevweek_tokens() {
  agentsview activity report --preset week --date "$(date -v-7d +%Y-%m-%d)" --no-sync --json 2>/dev/null \
    | jq -r '[.buckets[].output_tokens] | add // 0'
}
# Publish the week view once (label WEEK) and hold it on screen: the DAY loop
# skips publishing while HOLD_FILE's timestamp is in the future.
# Humanize a token count: 5613320 -> "5.6M", 269880 -> "270k".
humanize() {
  local n="${1:-0}"
  if [ "${n:-0}" -ge 1000000 ] 2>/dev/null; then
    awk -v n="$n" 'BEGIN{printf "%.1fM", n/1000000}'
  else
    echo "$(( (n + 500) / 1000 ))k"
  fi
}

publish_week() {
  agentsview activity report --preset week --json >/dev/null 2>&1   # warm/sync once
  local used cl cx prev sub payload
  used="$(week_tokens)"; cl="$(week_tokens claude)"; cx="$(week_tokens codex)"; prev="$(prevweek_tokens)"
  : "${used:=0}"; : "${cl:=0}"; : "${cx:=0}"; : "${prev:=0}"
  sub="last wk $(humanize "$prev")"      # bottom line: previous week for comparison
  payload="$(jq -cn --argjson u "$used" --argjson l "$WEEK_LIMIT" --arg lab "WEEK" \
    --argjson p "$prev" --arg pl "wk" --argjson cl "$cl" --argjson cx "$cx" --arg sub "$sub" \
    '{meter:{used:$u,limit:$l,label:$lab,prev:$p,prevlab:$pl,cl:$cl,cx:$cx,sub:$sub}}')"
  mosquitto_pub "${pub_args[@]}" -m "$payload"
  echo "$(( $(date +%s) + WEEK_HOLD ))" > "$HOLD_FILE"
  echo "$(date +%T) → WEEK $payload  (hold ${WEEK_HOLD}s)"
}

# Previous active day is stable within a run — compute it once (day metric only).
PREV_TOK=0; PREV_LAB=""
if [ "$METRIC" != "peak" ]; then
  IFS=$'\t' read -r PREV_TOK PREV_LAB < <(prev_active_day)
  [ -z "$PREV_TOK" ] && PREV_TOK=0
fi

# Map a load level to a mood face across 5 zones: relax → panic.
zoneface() {  # $1=used $2=limit
  local p=0
  [ "${2:-0}" -gt 0 ] 2>/dev/null && p=$(( $1 * 100 / $2 ))
  if   [ "$p" -lt 20 ]; then echo bmo_bliss       # calm
  elif [ "$p" -lt 40 ]; then echo bmo_happy       # productive
  elif [ "$p" -lt 60 ]; then echo bmo_neutral     # in the zone
  elif [ "$p" -lt 80 ]; then echo bmo_worried     # heating up
  else                       echo bmo_panic; fi   # panic (shaking)
}

publish_once() {
  local used tag payload cl=0 cx=0 tot=0 face=""
  if [ "$METRIC" = "peak" ]; then
    local sid; sid="$(current_session)"
    [ -z "$sid" ] && { echo "$(date +%T) no session found" >&2; return 1; }
    used="$(peak_tokens "$sid")"; tag="ctx ${sid:0:8}"
  else
    used="$(today_output_tokens)"          # warms the DB (no --no-sync)
    cl="$(model_today claude)"; cx="$(model_today codex)"
    [ "$SHOW_TOTAL" = "1" ] && tot="$(cached_total)"
    [ "$MOOD_FACE" = "1" ] && face="$(zoneface "$used" "$METER_LIMIT")"
    tag="today out=$used face=${face:-none}"
  fi
  { [ -z "$used" ] || [ "$used" = "null" ]; } && used=0
  : "${tot:=0}"
  payload="$(jq -cn --argjson u "$used" --argjson l "$METER_LIMIT" --arg lab "$METER_LABEL" \
    --argjson p "${PREV_TOK:-0}" --arg pl "${PREV_LAB:-}" \
    --argjson cl "${cl:-0}" --argjson cx "${cx:-0}" --argjson tot "${tot:-0}" --arg face "$face" \
    '{meter:{used:$u,limit:$l,label:$lab,prev:$p,prevlab:$pl,cl:$cl,cx:$cx,tot:$tot,face:$face}}')"
  mosquitto_pub "${pub_args[@]}" -m "$payload" \
    && echo "$(date +%T) → $payload  ($tag)"
}

# One-shot week view (Mon–Sun) on request, e.g. `tabbie-tokenmeter.sh week`.
if [ "${1:-}" = "week" ]; then
  publish_week
  exit $?
fi

if [ "${ONCE:-0}" = "1" ]; then
  publish_once
  exit $?
fi

echo "tabbie-tokenmeter: every ${INTERVAL}s → ${HOST}:${PORT} ${TOPIC} (limit ${METER_LIMIT})"
while true; do
  # Respect a week-view hold: leave the WEEK snapshot on screen, don't overwrite.
  if [ -f "$HOLD_FILE" ]; then
    if [ "$(date +%s)" -lt "$(cat "$HOLD_FILE" 2>/dev/null || echo 0)" ]; then
      sleep "$INTERVAL"; continue
    fi
    rm -f "$HOLD_FILE"
  fi
  publish_once
  sleep "$INTERVAL"
done

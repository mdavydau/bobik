#!/usr/bin/env bash
# tabbie-ctl — control a Tabbie over its HTTP API from any machine.
# Designed to be called by a voice assistant, a script, or by hand.
#
#   tabbie-ctl <expression> [task text]   set a face
#   tabbie-ctl status                     print device status (JSON)
#   tabbie-ctl list                       list expressions
#
# Expressions (friendly word -> firmware animation):
#   angry|mad            -> paused      (animated angry face)
#   happy|love           -> love
#   done|complete        -> complete
#   focus|work           -> focus
#   break|relax|rest     -> break
#   pomodoro|timer       -> pomodoro
#   idle|neutral|normal  -> idle
#   hello|startup|boot   -> startup
# Any other word is passed through as a raw animation name.
#
# Host: set TABBIE_HOST (default tabbie.local). Examples:
#   tabbie-ctl angry
#   TABBIE_HOST=192.168.233.229 tabbie-ctl focus "writing the report"
set -uo pipefail
HOST="${TABBIE_HOST:-tabbie.local}"
BASE="http://$HOST"

usage() { sed -n '2,28p' "$0" | sed 's/^# \{0,1\}//'; }

cmd="${1:-status}"; shift || true
task="${*:-}"

case "$cmd" in
  ""|-h|--help|help) usage; exit 0 ;;
  list)
    echo "angry happy done focus break pomodoro idle hello sweat coffee  (or any raw animation name)"; exit 0 ;;
  status)
    if ! curl -fsS -m 6 "$BASE/api/status"; then
      echo "ERROR: cannot reach Tabbie at $BASE" >&2; exit 1
    fi
    echo; exit 0 ;;
esac

# map friendly word -> animation
case "$cmd" in
  angry|mad)            anim=paused ;;
  happy|love)           anim=love ;;
  done|complete|celebrate) anim=complete ;;
  focus|work)           anim=focus ;;
  break|relax|rest|calm) anim=break ;;
  pomodoro|timer)       anim=pomodoro ;;
  idle|neutral|normal)  anim=idle ;;
  hello|startup|boot)   anim=startup ;;
  hot|sweat|tired)      anim=sweat ;;
  coffee|brew|break-coffee) anim=coffee ;;
  *)                    anim="$cmd" ;;   # raw passthrough
esac

resp=$(curl -fsS -m 6 -X POST "$BASE/api/animation" \
  -H "Content-Type: application/json" \
  -d "{\"animation\":\"$anim\",\"task\":\"$task\"}" 2>/dev/null) || {
    echo "ERROR: cannot reach Tabbie at $BASE" >&2; exit 1; }

echo "$resp"
case "$resp" in
  *'"success":true'*) exit 0 ;;
  *) echo "WARN: device did not confirm success" >&2; exit 2 ;;
esac

#!/usr/bin/env bash
# tabbie-pub — control Tabbie by PUBLISHING to the MQTT broker.
# Use this on the server / from the voice assistant when the board reaches
# Tabbie over MQTT (board dials out to the broker; works behind home NAT).
#
# Requires mosquitto_pub  (Debian/Ubuntu: apt install mosquitto-clients;
#                          macOS: brew install mosquitto)
#
#   tabbie-pub <expression> [task text]
#   tabbie-pub angry
#   tabbie-pub dev-on
#   tabbie-pub dev-off
#   tabbie-pub debug-on
#   tabbie-pub debug-off
#   tabbie-pub mochi-happy
#   tabbie-pub status-alert
#   tabbie-pub sleepy
#   tabbie-pub stop
#   tabbie-pub focus "writing the report"
#
# Config via env (or edit defaults below):
#   MQTT_HOST  (default localhost)   MQTT_PORT (default 1883)
#   MQTT_TOPIC (default tabbie/cmd)
#   MQTT_USER  MQTT_PASS             (optional broker auth)
set -uo pipefail

HOST="${MQTT_HOST:-localhost}"
PORT="${MQTT_PORT:-1883}"
TOPIC="${MQTT_TOPIC:-tabbie/cmd}"

cmd="${1:-}"; shift || true
task="${*:-mqtt}"
[ -z "$cmd" ] && { echo "usage: tabbie-pub <expression> [task]"; exit 1; }

payload=""
case "$cmd" in
  dev|dev-toggle|debug|debug-toggle) payload='{"dev":"toggle"}' ;;
  dev-on|dev-enable|devmode-on|debug-on|debug-enable)   payload='{"dev":true}' ;;
  dev-off|dev-disable|devmode-off|debug-off|debug-disable) payload='{"dev":false}' ;;
  stop|stop-escalation|cancel-escalation|bobik-stop) payload='{"animation":"idle","task":"stop-escalation"}' ;;
  angry|mad)               anim=paused ;;
  happy|love)              anim=love ;;
  done|complete|celebrate) anim=complete ;;
  focus|work)              anim=focus ;;
  break|relax|rest|calm)   anim=break ;;
  pomodoro|timer)          anim=pomodoro ;;
  idle|neutral|normal)     anim=idle ;;
  hello|startup|boot)      anim=startup ;;
  hot|sweat|tired)         anim=sweat ;;
  sleepy|sleep|night|zzz)  anim=sleepy ;;
  coffee|brew)             anim=coffee ;;
  mochi-happy|mochi_happy) anim=mochi_happy ;;
  mochi-angry|mochi_angry) anim=mochi_angry ;;
  mochi-love|mochi_love)   anim=mochi_love ;;
  big-smile|big_smile|upiir-big-smile|upiir_big_smile) anim=upiir_big_smile ;;
  status-alert|status_alert|status-reminder|status_reminder) anim=status_alert ;;
  *)                       anim="$cmd" ;;
esac

auth=()
[ -n "${MQTT_USER:-}" ] && auth+=(-u "$MQTT_USER")
[ -n "${MQTT_PASS:-}" ] && auth+=(-P "$MQTT_PASS")

[ -n "$payload" ] || payload="{\"animation\":\"$anim\",\"task\":\"$task\"}"
mosquitto_pub -h "$HOST" -p "$PORT" ${auth[@]+"${auth[@]}"} -t "$TOPIC" -m "$payload" \
  && echo "published -> $TOPIC : $payload" \
  || { echo "ERROR: publish failed (broker $HOST:$PORT)" >&2; exit 1; }

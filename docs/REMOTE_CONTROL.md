# Controlling Tabbie remotely (incl. from a voice assistant)

Tabbie runs a small HTTP server on the ESP32. Anything that can reach the board
on the network can drive its face with a single HTTP request — no app required.
This doc covers the API, a helper CLI, how to reach the board from another
machine (e.g. a server running a voice assistant), and the security caveats.

> **About the servo / "motor":** the firmware calls `moveServoTo()` for a neck
> servo on **GPIO 13**. If no servo is attached those calls are harmless no-ops —
> the ESP32 just writes a PWM value to an unconnected pin. Faces work without any
> motor. Add a servo later and head-movement starts working with no code change.

---

## 1. HTTP API

Base URL: `http://<tabbie-host>` where `<tabbie-host>` is `tabbie.local`
(mDNS) or the board's LAN IP (e.g. `192.168.233.229`). CORS is open (`*`).

| Method | Path | Body | Purpose |
|--------|------|------|---------|
| `GET`  | `/api/status` | — | JSON: status, current animation, ip, ssid, rssi, uptime |
| `POST` | `/api/animation` | `{"animation":"<name>","task":"<text>","duration":<sec>}` | Set the face. `duration` only used by `focus`. |
| `POST` | `/api/servo` | (test payload) | Manually exercise the neck servo |
| `POST` | `/api/debug` | — | Show device info on screen briefly |
| `POST` | `/api/reset` | — | Clear WiFi creds and reboot into setup mode |

`GET /api/status` also includes `devMode`. Over MQTT, publish `{"dev":true}`,
`{"dev":false}`, or `{"dev":"toggle"}` to show or hide the persistent on-device
DevMode diagnostics screen. The helper supports `tools/tabbie-pub.sh dev-on`,
`dev-off`, `debug-on`, and `debug-off` aliases for the same screen.

### Expressions (`animation` values)

`idle` · `focus` · `break` · `paused` (animated **angry**) · `love` ·
`pomodoro` · `complete` · `startup` · `sweat` · `coffee` ·
`mochi_happy` · `mochi_angry` · `mochi_love` · `upiir_big_smile` ·
`status_alert`

Friendly helper aliases: `mochi-happy`, `mochi-angry`, `mochi-love`,
`big-smile`, `status-alert`.

### Examples

```bash
# status
curl -s http://tabbie.local/api/status

# angry face (the animated one), with a label
curl -s -X POST http://tabbie.local/api/animation \
  -H "Content-Type: application/json" \
  -d '{"animation":"paused","task":"after hours - go rest"}'

# focus for 25 minutes
curl -s -X POST http://tabbie.local/api/animation \
  -H "Content-Type: application/json" \
  -d '{"animation":"focus","duration":1500}'
```

---

## 2. The `tabbie-ctl` helper

`tools/tabbie-ctl.sh` wraps the API with friendly words, ideal for a voice
assistant to shell out to.

```bash
tools/tabbie-ctl.sh angry              # -> paused (animated angry)
tools/tabbie-ctl.sh focus "report"     # -> focus, labelled
tools/tabbie-ctl.sh status             # JSON status
TABBIE_HOST=192.168.233.229 tools/tabbie-ctl.sh happy
```

Friendly words: `angry happy done focus break pomodoro idle hello` (plus any raw
animation name). Set `TABBIE_HOST` to the board's hostname/IP (default
`tabbie.local`).

---

## 3. Reaching the board from another machine

The board is a normal HTTP host on your LAN. How a *server* reaches it depends on
where that server lives.

### A. Server is on the same network (simplest)
Use the board directly — nothing else needed:
```bash
TABBIE_HOST=tabbie.local tools/tabbie-ctl.sh angry
```
If `tabbie.local` doesn't resolve on the server, use the IP (find it in
`/api/status` while on the LAN, or your router's client list). Consider giving
the board a **DHCP reservation** so its IP never changes.

### B. Server is remote (different network) — recommended: Tailscale
The board sits behind your home router's NAT, so a remote server can't reach
`192.168.x.x` directly. The board can't run a VPN client itself, so put an
**always-on machine on the home LAN** (a Raspberry Pi, mini-PC, or any box) on a
[Tailscale](https://tailscale.com) tailnet, then either:

- **Subnet router** — enable subnet routes for your LAN (e.g. `192.168.233.0/24`)
  on that home node (`tailscale up --advertise-routes=192.168.233.0/24`). The
  remote server, also on the tailnet, can then hit `http://192.168.233.229`
  directly. Run `tabbie-ctl` from the server.
- **SSH hop** — simplest if you don't want subnet routing: the server runs
  `ssh homenode 'TABBIE_HOST=192.168.233.229 ~/tabbie/tools/tabbie-ctl.sh angry'`.

This keeps the board off the public internet entirely.

### C. Router port-forward (quick, **insecure**)
Forward an external port to the board's port 80. ⚠️ The board has **no
authentication** — exposing it publicly lets anyone change the face or factory-
reset WiFi. Only do this behind a reverse proxy that adds auth/TLS, or don't.

### D. MQTT bridge (most robust for remote)
Have the ESP32 **dial out** to an MQTT broker running on your server and
subscribe to a command topic. This traverses NAT with no home node and no port-
forwarding — the board initiates the connection. This is implemented with
`PubSubClient`; enable it by creating `firmware/src/mqtt_config.h`.
See [`MQTT_BRIDGE.md`](MQTT_BRIDGE.md).

---

## 4. Voice-assistant integration

Map intents to shell calls (or direct HTTP). Examples:

```
intent "make tabbie angry"   -> tabbie-ctl angry
intent "tabbie focus mode"   -> tabbie-ctl focus
intent "tabbie how are you"  -> tabbie-ctl status   (then read back the JSON)
```

If your assistant can only do HTTP, point it at
`POST http://<host>/api/animation` with the JSON body above.

---

## 5. Running without your laptop

The normal daily mood schedule now runs on the ESP32 itself, so it does not
depend on a laptop or cron. The board handles the 16:00 escalation, `sleepy`
from 18:00 to 08:00, and the morning `mochi_happy` wake-up after NTP time sync.
The legacy `tools/tabbie_mood.sh` script is only for manual experiments.

---

## 6. Security notes

- No auth + open CORS: treat the API as **LAN-only** unless fronted by a proxy
  that adds authentication.
- `/api/reset` wipes saved WiFi credentials — keep it unreachable from untrusted
  networks.
- Prefer Tailscale (§3B) over port-forwarding (§3C).

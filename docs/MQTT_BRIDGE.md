# MQTT bridge — control Tabbie from a remote server / voice assistant

When your controller (e.g. a voice assistant) runs on a **remote server** and the
only always-on device at home is the ESP32 itself, this is the robust path: the
board **dials out** to an MQTT broker on your server and listens for commands.
No port-forwarding, no NAT holes, no always-on home box.

```
voice assistant ──publish──▶ tabbie/cmd  ┐
                                          ├─ MQTT broker (your server)
Tabbie (ESP32) ──subscribe──────────────▶┘   (board connected outbound)
Tabbie ──publish "online"──▶ tabbie/status
```

Verified working on the LAN with Mosquitto before writing this.

---

## 1. Enable the bridge in firmware

The bridge compiles **only** when `firmware/src/mqtt_config.h` exists (so the
repo still builds without it, and your broker creds stay out of git).

```bash
cd firmware/src
cp mqtt_config.example.h mqtt_config.h
$EDITOR mqtt_config.h        # set MQTT_HOST, port, MQTT_USER/MQTT_PASS
cd .. && pio run --target upload
```

`mqtt_config.h` is gitignored. Key fields:

| Define | Meaning |
|--------|---------|
| `MQTT_HOST` | your server's public hostname or IP |
| `MQTT_PORT` | `1883` (plain MQTT; TLS is not built in — see §5) |
| `MQTT_USER` / `MQTT_PASS` | broker credentials (comment out for anonymous) |
| `MQTT_CMD_TOPIC` | topic the board subscribes to (`tabbie/cmd`) |
| `MQTT_STATUS_TOPIC` | topic the board publishes `online` to (`tabbie/status`) |

On boot the board connects, subscribes to `MQTT_CMD_TOPIC`, retries every 5s if
the broker is down, and never blocks the display loop.

### macOS flashing notes

Install the CLI tools once:

```bash
brew install platformio mosquitto
```

Check that the ESP32 is visible:

```bash
pio device list
# or:
ls /dev/cu.*
```

Most boards appear as a CP2102/CP210x serial device such as
`/dev/cu.usbserial-0001`. If no USB serial port appears, install the Silicon
Labs driver:

```bash
brew install --cask silicon-labs-vcp-driver
```

The firmware uses `huge_app.csv` and explicit PlatformIO dependencies so clean
builds work reproducibly with the MQTT and animation assets. A first clean build
can take several minutes; repeat builds should normally finish in a few seconds.

---

## 2. Run a broker on your server (Mosquitto)

```bash
# Debian/Ubuntu
sudo apt install mosquitto mosquitto-clients

# create a user (recommended)
sudo mosquitto_passwd -c /etc/mosquitto/passwd tabbie

# /etc/mosquitto/conf.d/tabbie.conf
listener 1883 0.0.0.0
allow_anonymous false
password_file /etc/mosquitto/passwd

sudo systemctl restart mosquitto
```

Open port **1883** to the board's source (your home IP) in the server firewall.
Or run it in Docker:

```bash
docker run -d --name mosquitto -p 1883:1883 \
  -v "$PWD/mosquitto.conf:/mosquitto/config/mosquitto.conf" \
  -v "$PWD/passwd:/mosquitto/config/passwd" eclipse-mosquitto
```

---

## 3. Send commands

Commands published to `tabbie/cmd` may be **JSON** or a **bare animation name**:

```bash
# JSON
mosquitto_pub -h YOUR_SERVER -u tabbie -P secret -t tabbie/cmd \
  -m '{"animation":"paused","task":"go rest"}'

# persistent OLED DevMode diagnostics screen
mosquitto_pub -h YOUR_SERVER -u tabbie -P secret -t tabbie/cmd \
  -m '{"dev":true}'
mosquitto_pub -h YOUR_SERVER -u tabbie -P secret -t tabbie/cmd \
  -m '{"dev":false}'
mosquitto_pub -h YOUR_SERVER -u tabbie -P secret -t tabbie/cmd \
  -m '{"dev":"toggle"}'

# bare name
mosquitto_pub -h YOUR_SERVER -u tabbie -P secret -t tabbie/cmd -m love
mosquitto_pub -h YOUR_SERVER -u tabbie -P secret -t tabbie/cmd -m "dev on"
mosquitto_pub -h YOUR_SERVER -u tabbie -P secret -t tabbie/cmd -m "dev off"

# stop today's 16:00-17:00 escalation
mosquitto_pub -h YOUR_SERVER -u tabbie -P secret -t tabbie/cmd \
  -m '{"animation":"idle","task":"stop-escalation"}'
```

Or use the helper `tools/tabbie-pub.sh` (friendly words → JSON):

```bash
MQTT_HOST=YOUR_SERVER MQTT_USER=tabbie MQTT_PASS=secret \
  tools/tabbie-pub.sh angry
MQTT_HOST=YOUR_SERVER MQTT_USER=tabbie MQTT_PASS=secret \
  tools/tabbie-pub.sh dev-on
MQTT_HOST=YOUR_SERVER MQTT_USER=tabbie MQTT_PASS=secret \
  tools/tabbie-pub.sh dev-off
MQTT_HOST=YOUR_SERVER MQTT_USER=tabbie MQTT_PASS=secret \
  tools/tabbie-pub.sh debug-on
MQTT_HOST=YOUR_SERVER MQTT_USER=tabbie MQTT_PASS=secret \
  tools/tabbie-pub.sh stop
```

Animations: `idle focus break paused`(animated angry)` love pomodoro complete startup sweat sleepy coffee mochi_happy mochi_angry mochi_love upiir_big_smile status_alert`.
DevMode is the internal debug screen. It is a live feature flag toggled over
MQTT, so switching it on/off does not require reflashing. It stays on until
switched off and rotates through WiFi/MQTT, current face/task/schedule, and
system values. The helper accepts both `dev-*` and `debug-*` aliases for this
screen. Use `/api/debug` for the shorter 8-second status screen. While DevMode
is on, the firmware also writes a compact snapshot to the USB serial log every
5 seconds.

Mochi-style faces can be switched over MQTT with friendly aliases:

```bash
tools/tabbie-pub.sh mochi-happy
tools/tabbie-pub.sh mochi-angry
tools/tabbie-pub.sh mochi-love
tools/tabbie-pub.sh big-smile
tools/tabbie-pub.sh status-alert
tools/tabbie-pub.sh sleepy
```

---

## 4. Voice-assistant integration

On the server, the voice assistant should call `tools/tabbie-pub.sh`. The
script publishes to `tabbie/cmd`; Bobik receives the command over MQTT and
changes the face without reflashing.

Server setup:

```bash
cd /root/bobik
source .mqtt.env

# Smoke test: should switch the device immediately.
tools/tabbie-pub.sh status-alert "voice assistant smoke test"
```

Expected `.mqtt.env` variables:

```bash
export MQTT_HOST=localhost
export MQTT_PORT=1883
export MQTT_USER=tabbie
export MQTT_PASS='your-broker-password'
export MQTT_TOPIC=tabbie/cmd
```

Intent mapping:

```
"make tabbie angry"       -> tools/tabbie-pub.sh angry
"tabbie focus mode"       -> tools/tabbie-pub.sh focus
"tabbie celebrate"        -> tools/tabbie-pub.sh complete
"tabbie mochi happy"      -> tools/tabbie-pub.sh mochi-happy
"tabbie mochi angry"      -> tools/tabbie-pub.sh mochi-angry
"tabbie mochi love"       -> tools/tabbie-pub.sh mochi-love
"tabbie big smile"        -> tools/tabbie-pub.sh big-smile
"tabbie status reminder"  -> tools/tabbie-pub.sh status-alert
"tabbie sleepy"           -> tools/tabbie-pub.sh sleepy
"tabbie dev mode on"      -> tools/tabbie-pub.sh dev-on
"tabbie dev mode off"     -> tools/tabbie-pub.sh dev-off
"bobik stop"              -> tools/tabbie-pub.sh stop
```

Watch the board come/go online:

```bash
source .mqtt.env
mosquitto_sub -h "$MQTT_HOST" -p "$MQTT_PORT" -u "$MQTT_USER" -P "$MQTT_PASS" -t tabbie/status
```

Check the currently shown face from the LAN:

```bash
curl -s http://192.168.233.229/api/status
```

Direct raw MQTT payloads also work if the assistant cannot call shell scripts:

```bash
mosquitto_pub -h "$MQTT_HOST" -p "$MQTT_PORT" -u "$MQTT_USER" -P "$MQTT_PASS" \
  -t tabbie/cmd -m '{"animation":"status_alert","task":"voice assistant"}'
```

The after-hours "angry when working past 18:00" rule can also run on the server
now — point `tabbie_mood.sh`'s send step at `tabbie-pub` instead of curl.

---

## 5. Security

- The board uses **plain MQTT (1883)** — fine on a trusted network/VPN, but over
  the public internet anyone who reaches the broker can drive your Tabbie.
  **Always set a broker username/password** (§2) and restrict port 1883 by source
  IP. Avoid anonymous brokers except for LAN testing.
- TLS (port 8883) isn't compiled in to keep flash small; if you need it, add
  `WiFiClientSecure` + the broker CA and rebuild (there's now plenty of flash
  with the `huge_app` partition).

---

## 6. Local LAN test (what was used to verify this)

```bash
# on a Mac/PC on the same WiFi as Tabbie
brew install mosquitto
printf 'listener 1883 0.0.0.0\nallow_anonymous true\n' > /tmp/m.conf
mosquitto -c /tmp/m.conf -v        # note this machine's LAN IP

# set MQTT_HOST in mqtt_config.h to that LAN IP, reflash, then:
mosquitto_pub -h <lan-ip> -t tabbie/cmd -m '{"animation":"love"}'
```

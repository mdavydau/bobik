# Flashing Bobik from macOS

This is the short path for rebuilding and flashing the ESP32 firmware from a
Mac.

## 1. Install tools

```bash
brew install platformio mosquitto
```

If the ESP32 does not appear as a serial port, install the CP210x driver:

```bash
brew install --cask silicon-labs-vcp-driver
```

Check the board:

```bash
pio device list
# expected: CP2102 / CP210x device, often /dev/cu.usbserial-0001
```

## 2. Create local MQTT config

```bash
cp firmware/src/mqtt_config.example.h firmware/src/mqtt_config.h
$EDITOR firmware/src/mqtt_config.h
```

Set:

```cpp
#define MQTT_HOST         "your-broker-host"
#define MQTT_PORT         1883
#define MQTT_USER         "your-user"
#define MQTT_PASS         "your-password"
```

`firmware/src/mqtt_config.h` is ignored by git. Do not commit it.

## 3. Build and upload

```bash
cd firmware
pio run
pio run --target upload --upload-port /dev/cu.usbserial-0001
```

If upload cannot connect, hold the ESP32 `BOOT` button while the upload command
is connecting.

The first clean build can take several minutes because U8g2 and the animation
headers compile from scratch. Repeat builds should be much faster.

## Preview the OLED before flashing

The app includes a local 128x64 OLED preview page. Use it to check the screen
layout and the WiFi/MQTT status overlay before uploading firmware:

```bash
cd app
PATH=/opt/homebrew/opt/node@22/bin:$PATH npm run dev -- --host 127.0.0.1
```

Open:

```text
http://127.0.0.1:8080/oled-preview.html
```

The preview can run in manual mode or poll the real device. Leave `Device URL`
as `http://tabbie.local`, or paste the direct IP address shown on the device
debug screen, for example `http://192.168.233.229`, if macOS mDNS resolution is
slow. The `PNG` button saves the current preview frame for screenshots.

When a new face header is added under `firmware/src/`, refresh the preview
assets:

```bash
python3 tools/export_oled_animations.py
```

This regenerates `app/public/animations/*.json` and `manifest.json`, and the
preview page will automatically show every exported face in the `Screen` menu.

## Mac tools for previewing and monitoring

Use these tools for different jobs:

| Tool | Best for | Install / run |
| --- | --- | --- |
| Local OLED preview | Fast face/layout checks from exported firmware assets | `cd app && PATH=/opt/homebrew/opt/node@22/bin:$PATH npm run dev -- --host 127.0.0.1` |
| Wokwi for VS Code | Full ESP32 + virtual OLED simulation without flashing hardware | Install VS Code, then install the `Wokwi Simulator` extension |
| Wokwi CLI | Terminal/CI Wokwi simulations after the VS Code setup is working | `curl -L https://wokwi.com/ci/install.sh \| sh` |
| PlatformIO monitor | Live logs from the real ESP32 over USB | `cd firmware && pio device monitor -p /dev/cu.usbserial-0001 -b 115200` |
| Arduino IDE | GUI serial monitor / serial plotter for the real ESP32 | `brew install --cask arduino-ide` |

Wokwi needs two project files in the repo root when we wire it up:
`wokwi.toml` points at the compiled firmware/ELF, and `diagram.json` describes
the ESP32 + OLED wiring. For Bobik the useful target is an ESP32 plus a 128x64
I2C SSD1306-compatible OLED. Our physical display is SH1106, but the preview
and current firmware drawing are 128x64 monochrome, so Wokwi is still useful for
screen/layout iteration before real flashing.

## 4. Configure WiFi

WiFi credentials are saved on the ESP32 in Preferences. A normal firmware upload
does not erase them.

For a new board or after reset:

1. Connect to the `Tabbie-Setup` WiFi network.
2. Open the setup page shown by the board.
3. Save the home WiFi SSID/password.
4. Let the ESP32 reboot and reconnect.

## 5. Test MQTT

Watch status:

```bash
mosquitto_sub -h your-broker-host -p 1883 -u your-user -P 'your-password' -t tabbie/status
```

Send a command:

```bash
mosquitto_pub -h your-broker-host -p 1883 -u your-user -P 'your-password' \
  -t tabbie/cmd -m '{"animation":"love","task":"mac test"}'
```

Or use the helper:

```bash
MQTT_HOST=your-broker-host MQTT_USER=your-user MQTT_PASS='your-password' \
  tools/tabbie-pub.sh love
```

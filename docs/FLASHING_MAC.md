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

// MQTT bridge configuration — COPY this file to `mqtt_config.h` and fill in your
// broker. When mqtt_config.h exists, the firmware enables the MQTT bridge so a
// remote server / voice assistant can control Tabbie by publishing commands.
//
//   cp mqtt_config.example.h mqtt_config.h   # then edit, then reflash
//
// mqtt_config.h is gitignored so your broker credentials never get committed.
#pragma once

#define MQTT_HOST         "broker.example.com"   // your server's hostname or IP
#define MQTT_PORT         1883                     // plain MQTT (TLS not built in)
#define MQTT_CLIENT_ID    "tabbie-1"

// Topics
#define MQTT_CMD_TOPIC    "tabbie/cmd"             // board SUBSCRIBES here for commands
#define MQTT_STATUS_TOPIC "tabbie/status"          // board PUBLISHES "online" here

// Auth — comment both out for an anonymous broker (LAN testing only).
#define MQTT_USER         "tabbie"
#define MQTT_PASS         "change-me"

// Commands published to MQTT_CMD_TOPIC may be either:
//   - JSON:  {"animation":"paused","task":"go rest","duration":0}
//   - or a bare animation name:  paused
// Animations: idle focus break paused(animated angry) love pomodoro complete startup

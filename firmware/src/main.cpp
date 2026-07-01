#include <Wire.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <U8g2lib.h>
#include <Preferences.h>
#include <ESPmDNS.h>
#include <DNSServer.h>
#include <ESP32Servo.h>
#include <time.h>

// ============================================
// SERVO CONFIGURATION (SIMPLIFIED)
// ============================================
const int SERVO_PIN = 13;
Servo neckServo;

// Servo positions (degrees)
const int SERVO_LEFT = 15;
const int SERVO_CENTER = 90;
const int SERVO_RIGHT = 165;

// Servo movement
int currentServoPos = SERVO_CENTER;
int targetServoPos = SERVO_CENTER;
const int SERVO_SPEED = 8; // degrees per step (lower = slower)

// Flag: Was animation triggered via API? (forces servo active on first loop)
bool animationTriggeredViaAPI = false;

// For idle: track loops for automatic mode (every 4th loop)
int idleLoopCount = 0;
const int IDLE_SERVO_EVERY_N_LOOPS = 4;

// For paused: shake timer
unsigned long lastPausedShakeTime = 0;

// For focus: progress tracking
unsigned long focusStartTime = 0;
unsigned long focusDuration = 0;
bool focusHalfwayDone = false;

// Animation data
#include "idle01.h"
#include "focus01.h"
#include "relax01.h"
#include "love01.h"
#include "startup01.h"
#include "angry_bitmap.h"  // Static angry image (fallback)
#include "angry01.h"          // Animated angry face
#include "sweat01.h"          // Animated hot/sweating face
#include "coffee01.h"          // face: coffee
#include "mochi_happy01.h"          // face: mochi_happy
#include "mochi_angry01.h"          // face: mochi_angry
#include "mochi_love01.h"          // face: mochi_love
#include "upiir_big_smile01.h"          // face: upiir_big_smile

// Optional MQTT bridge — enabled only when firmware/src/mqtt_config.h exists.
// Lets a remote server / voice assistant control Tabbie by publishing commands
// (the board dials OUT to the broker, so it works behind home NAT).
#if defined(__has_include)
  #if __has_include("mqtt_config.h")
    #include "mqtt_config.h"
    #include <PubSubClient.h>
    #define TABBIE_MQTT 1
  #endif
#endif

// OLED display configuration - Using U8g2 with SH1106 driver
U8G2_SH1106_128X64_NONAME_F_HW_I2C display(U8G2_R0, /* reset=*/ U8X8_PIN_NONE);

// Web server on port 80
WebServer server(80);

// DNS server for captive portal
DNSServer dnsServer;

#ifdef TABBIE_MQTT
WiFiClient mqttWifiClient;
PubSubClient mqttClient(mqttWifiClient);
unsigned long lastMqttReconnect = 0;
void mqttLoop();
void mqttCallback(char* topic, byte* payload, unsigned int length);
void applyAnimation(const String& anim, const String& task, unsigned long durSec);
#endif

// WiFi credentials storage
Preferences preferences;

// Current state
String currentAnimation = "startup";
String currentTask = "";
unsigned long animationStartTime = 0;
unsigned long startupTime = 0;
bool hasCompletedStartup = false;
bool isInSetupMode = false;
String wifiStatus = "disconnected";
String lastError = "";

// ============================================
// ON-DEVICE TIME & SCHEDULED ("time-related") FACES
// ============================================
// POSIX TZ for Europe/Warsaw (Poland): CET (+1) / CEST (+2) with EU DST rules.
#define TABBIE_TZ "CET-1CEST,M3.5.0,M10.5.0/3"

bool timeConfigStarted = false;      // configTzTime() has been requested
bool timeSynced = false;             // NTP returned a valid wall-clock time
unsigned long lastTimeSyncAttempt = 0;
unsigned long lastScheduleCheck = 0;

// A scheduled face fires once per day at hour:minute (device-local time),
// shows for `showSec`, then reverts to idle. Add rows for more time-of-day faces.
struct ScheduledFace {
  uint8_t hour;
  uint8_t minute;
  const char* animation;   // must be a wired animation name (see updateDisplay)
  const char* task;
  uint16_t showSec;        // hold time before returning to idle (0 = leave it up)
  int lastFiredYmd;        // internal: YYYYMMDD it last fired (0 = never)
};
ScheduledFace scheduledFaces[] = {
  // Noon coffee break, 12:00 Europe/Warsaw.
  { 12,  0, "coffee", "coffee time", 120, 0 },
  // Daily status report reminder, 14:30 Europe/Warsaw.
  { 14, 30, "status_alert", "STATUS STATUS STATUS", 300, 0 },
  // Examples to add later, e.g.:
  // {  9,  0, "focus",  "morning focus",  0, 0 },
  // { 18,  0, "sweat",  "wrap it up",   120, 0 },
};
const int scheduledFaceCount = sizeof(scheduledFaces) / sizeof(scheduledFaces[0]);

// Tracks a currently-showing scheduled face so we can auto-revert it to idle,
// but only if nothing else (API/MQTT) has taken over in the meantime.
String scheduledActiveAnim = "";
unsigned long scheduledActiveStart = 0;
unsigned long scheduledRevertAt = 0;

// WiFi connection state machine
String savedSSID = "";
String savedPassword = "";
unsigned long wifiConnectStartTime = 0;
bool wifiInitialized = false;
bool wifiConnecting = false;
bool webServerStarted = false;

const int MAX_WIFI_ATTEMPTS = 3;
int wifiAttemptCount = 0;
unsigned long wifiRetryWaitUntil = 0;

// Debug mode - shows device info on OLED when triggered
bool isDebugMode = false;
unsigned long debugModeStartTime = 0;
const unsigned long DEBUG_MODE_DURATION = 8000; // Show debug info for 8 seconds
bool devModeEnabled = false; // Persistent diagnostics screen toggled remotely
unsigned long lastDevLogAt = 0;

// Physical button for showing debug info
// Using GPIO27 - safe pin that's not a strapping pin
const int DEBUG_BUTTON_PIN = 27;
unsigned long lastButtonPress = 0;
const unsigned long BUTTON_DEBOUNCE_MS = 300; // Debounce time

// Setup mode configuration
const char* SETUP_SSID = "Tabbie-Setup";
const char* MDNS_NAME = "tabbie";

// Function declarations
void setupDisplay();
void loadWiFiCredentials();
void handleWiFiConnection();
void startSetupMode();
void startNormalMode();
void setupWebServer();
void setupMDNS();
void handleRoot();
void handleSetupPage();
void handleWiFiConfig();
void handleStatus();
void handleAnimation();
void handleWiFiSettings();
void handleCORS();
void triggerAnimation(const String& anim, const String& task, unsigned long durSec);
void syncTimeIfNeeded();
void checkScheduledFaces();
void logDevSnapshot();
void updateDisplay();
void drawSetupMode();
void drawConnecting();
void drawConnected();
void drawError();
void drawIdleAnimation();
void drawFocusAnimation();
void drawRelaxAnimation();
void drawLoveAnimation();
void drawStartupAnimation();
void drawAngryImage();
void drawAngryAnimation();
void drawSweatAnimation();
void drawPomodoroAnimation();
void drawTaskCompleteAnimation();
void drawCoffeeAnimation();
void drawMochi_happyAnimation();
void drawMochi_angryAnimation();
void drawMochi_loveAnimation();
void drawUpiir_big_smileAnimation();
void drawStatus_alertAnimation();
void drawDebugInfo();
void drawDevInfo();
String clipForDisplay(const String& value, int maxLen);
void drawStatusMark(int x, int y, bool ok);
void drawStatusOverlay();
void flushDisplay(bool showOverlay = true);
void setDevMode(bool enabled);
void handleDebug();
void handleReset();
void handleServoTest();
void checkDebugButton();
void prepareWiFiForRetry(unsigned long delayMs = 0);
void onWiFiConnectionFailure(const String& reason);

// Servo functions
void setupServo();
void moveServoTo(int position);
void updateServoMovement();

void setup() {
  Serial.begin(115200);
  Serial.println("🤖 Tabbie Assistant Starting...");
  
  // Record startup time
  startupTime = millis();
  
  // Setup debug button (GPIO0 = BOOT button on most ESP32 boards)
  pinMode(DEBUG_BUTTON_PIN, INPUT_PULLUP);
  
  // CRITICAL: Clean WiFi state from any previous boot/mode
  // This prevents issues when switching between AP and STA modes
  WiFi.persistent(false);  // Don't save WiFi config to flash
  WiFi.disconnect(true);   // Disconnect and clear saved credentials
  WiFi.mode(WIFI_OFF);     // Turn off WiFi completely
  delay(200);              // Give hardware time to reset
  wifiAttemptCount = 0;
  wifiRetryWaitUntil = 0;
  
  // Initialize components
  setupDisplay();
  setupServo();
  
  // Initialize preferences
  preferences.begin("tabbie", false);
  
  // Load WiFi credentials (don't connect yet - animations first!)
  loadWiFiCredentials();

#ifdef TABBIE_MQTT
  // Configure the MQTT client (connection happens later, once WiFi is up)
  mqttClient.setServer(MQTT_HOST, MQTT_PORT);
  mqttClient.setCallback(mqttCallback);
  mqttClient.setBufferSize(512);
  Serial.print("📡 MQTT bridge enabled - broker ");
  Serial.print(MQTT_HOST); Serial.print(":"); Serial.println(MQTT_PORT);
#endif

  // DON'T setup web server here - it will be started in startNormalMode() or startSetupMode()
  // after WiFi is properly initialized
  
  Serial.println("✅ Tabbie initialized - animations will play while WiFi connects");
}

void setupDisplay() {
  Wire.begin(21, 22);
  
  display.begin();
  display.clearBuffer();
  // Don't show "Starting..." text - just clear the display
  // Startup animation will begin immediately in loop()
  flushDisplay(false);
  
  Serial.println("✅ OLED Display initialized (U8g2 SH1106)");
}

void setupServo() {
  ESP32PWM::allocateTimer(0);
  neckServo.setPeriodHertz(50);
  neckServo.attach(SERVO_PIN, 500, 2400);
  neckServo.write(SERVO_CENTER);
  currentServoPos = SERVO_CENTER;
  targetServoPos = SERVO_CENTER;
  Serial.println("✅ Servo initialized on GPIO " + String(SERVO_PIN));
}

// Move servo to position (sets target, updateServoMovement does the actual moving)
void moveServoTo(int position) {
  targetServoPos = constrain(position, SERVO_LEFT, SERVO_RIGHT);
  Serial.print("🎯 Servo → ");
  Serial.print(targetServoPos);
  Serial.println("°");
}

// Call this in loop() - smoothly moves servo towards target
void updateServoMovement() {
  static unsigned long lastMove = 0;
  if (millis() - lastMove < 10) return; // 10ms between steps
  lastMove = millis();
  
  if (currentServoPos != targetServoPos) {
    if (currentServoPos < targetServoPos) {
      currentServoPos = min(currentServoPos + SERVO_SPEED, targetServoPos);
    } else {
      currentServoPos = max(currentServoPos - SERVO_SPEED, targetServoPos);
    }
    neckServo.write(currentServoPos);
  }
}

void loadWiFiCredentials() {
  Serial.println("📡 Loading WiFi credentials...");
  
  // Check saved preferences (from captive portal setup)
  savedSSID = preferences.getString("wifi_ssid", "");
  savedPassword = preferences.getString("wifi_password", "");
  
  if (savedSSID.length() > 0 && savedPassword.length() > 0) {
    Serial.print("📡 Found saved credentials for: ");
    Serial.println(savedSSID);
    wifiStatus = "connecting";
    wifiAttemptCount = 0;
    wifiRetryWaitUntil = 0;
  } else {
    Serial.println("🔧 No credentials - entering setup mode");
    startSetupMode();
  }
}

void prepareWiFiForRetry(unsigned long delayMs) {
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  delay(100);

  wifiInitialized = false;
  wifiConnecting = false;
  wifiConnectStartTime = 0;
  wifiRetryWaitUntil = delayMs > 0 ? millis() + delayMs : 0;
  
  // Reset web server flag so it restarts on new network
  webServerStarted = false;
}

void onWiFiConnectionFailure(const String& reason) {
  wifiConnecting = false;
  wifiInitialized = false;
  wifiConnectStartTime = 0;

  Serial.print("❌ WiFi connection failed: ");
  Serial.println(reason);

  if (wifiAttemptCount < MAX_WIFI_ATTEMPTS) {
    Serial.print("🔁 Retrying WiFi (");
    Serial.print(wifiAttemptCount);
    Serial.print("/");
    Serial.print(MAX_WIFI_ATTEMPTS);
    Serial.println(")...");
    prepareWiFiForRetry(1000);
    wifiStatus = "connecting";
    return;
  }

  Serial.println("🚫 WiFi retries exhausted. Entering setup mode.");
  wifiStatus = "failed";
  lastError = reason + " - " + savedSSID;
  wifiRetryWaitUntil = 0;
  wifiAttemptCount = 0;
  startSetupMode();
}

void handleWiFiConnection() {
  // Don't handle WiFi if in setup mode
  if (isInSetupMode) {
    return;
  }
  
  // Respect backoff window between retries
  if (!wifiInitialized && wifiStatus == "connecting" && wifiRetryWaitUntil != 0) {
    if ((long)(wifiRetryWaitUntil - millis()) > 0) {
      return;
    }
    wifiRetryWaitUntil = 0;
  }

  // Initialize WiFi when we're ready for a new attempt
  if (!wifiInitialized && wifiStatus == "connecting") {
    wifiAttemptCount++;

    Serial.print("📡 Starting WiFi connection to: ");
    Serial.print(savedSSID);
    Serial.print(" (attempt ");
    Serial.print(wifiAttemptCount);
    Serial.print("/");
    Serial.print(MAX_WIFI_ATTEMPTS);
    Serial.println(")");

    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
#ifdef ARDUINO_ARCH_ESP32
    WiFi.setAutoConnect(true);
#endif
    WiFi.begin(savedSSID.c_str(), savedPassword.c_str());

    wifiInitialized = true;
    wifiConnecting = true;
    wifiConnectStartTime = millis();
    Serial.println("📡 WiFi initialized, connecting...");
    return;
  }

  // Check connection progress
  if (wifiConnecting) {
    wl_status_t status = WiFi.status();

    if (status == WL_CONNECTED) {
      Serial.print("✅ WiFi connected! IP: ");
      Serial.println(WiFi.localIP());
      wifiStatus = "connected";
      wifiConnecting = false;
      wifiInitialized = true;
      wifiRetryWaitUntil = 0;
      wifiAttemptCount = 0;
      startNormalMode();
      return;
    }

    if (status == WL_CONNECT_FAILED) {
      onWiFiConnectionFailure("CONNECT_FAILED");
      return;
    }

    if (status == WL_NO_SSID_AVAIL) {
      onWiFiConnectionFailure("NO_SSID");
      return;
    }

    if (status == WL_DISCONNECTED && (millis() - wifiConnectStartTime) > 7000) {
      onWiFiConnectionFailure("DISCONNECTED");
      return;
    }

    if (millis() - wifiConnectStartTime > 15000) {
      onWiFiConnectionFailure("Timeout");
      return;
    }
  }

  // Handle unexpected disconnections once connected
  if (wifiStatus == "connected" && WiFi.status() != WL_CONNECTED) {
    static unsigned long lastReconnect = 0;
    if (millis() - lastReconnect > 30000) {
      Serial.println("📡 Reconnecting...");
      wifiStatus = "connecting";
      wifiInitialized = false;
      wifiConnecting = false;
      wifiAttemptCount = 0;
      prepareWiFiForRetry(500);
      lastReconnect = millis();
    }
  }
}

void startSetupMode() {
  Serial.println("🔍 DEBUG: Starting startSetupMode()");
  isInSetupMode = true;
  wifiStatus = "setup";
  wifiAttemptCount = 0;
  wifiRetryWaitUntil = 0;
  
  Serial.println("🔧 Starting setup mode...");
  
  // Properly clean up any STA mode state before starting AP
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  delay(100);
  
  // Start in AP mode for setup
  WiFi.mode(WIFI_AP);
  Serial.println("🔍 DEBUG: WiFi mode set to AP");
  
  bool apResult = WiFi.softAP(SETUP_SSID);
  Serial.print("🔍 DEBUG: WiFi.softAP() result: ");
  Serial.println(apResult ? "success" : "failed");
  
  // Start DNS server for captive portal
  dnsServer.start(53, "*", WiFi.softAPIP());
  Serial.println("🔍 DEBUG: DNS server started");
  
  // Setup web server now that WiFi is initialized
  setupWebServer();
  
  Serial.print("📶 Setup WiFi started - SSID: ");
  Serial.println(SETUP_SSID);
  Serial.print("🌐 Setup IP: ");
  Serial.println(WiFi.softAPIP());
  
  updateDisplay();
  Serial.println("🔍 DEBUG: startSetupMode() completed");
}

void startNormalMode() {
  Serial.println("🔍 DEBUG: Starting startNormalMode()");
  isInSetupMode = false;
  wifiStatus = "connected";
  wifiAttemptCount = 0;
  wifiRetryWaitUntil = 0;
  
  Serial.print("🔍 DEBUG: WiFi status in normal mode: ");
  Serial.println(WiFi.status());
  Serial.print("🔍 DEBUG: IP address: ");
  Serial.println(WiFi.localIP());
  
  // Setup web server now that WiFi is initialized
  setupWebServer();
  
  // Setup mDNS
  setupMDNS();
  
  Serial.println("✅ Normal mode started");
  updateDisplay();
  Serial.println("🔍 DEBUG: startNormalMode() completed");
}

void setupMDNS() {
  if (MDNS.begin(MDNS_NAME)) {
    MDNS.addService("http", "tcp", 80);
    Serial.print("✅ mDNS started: ");
    Serial.print(MDNS_NAME);
    Serial.println(".local");
  } else {
    Serial.println("❌ mDNS setup failed");
  }
}

void setupWebServer() {
  // Only setup once
  if (webServerStarted) {
    return;
  }
  
  // Handle captive portal redirect
  server.onNotFound([]() {
    if (isInSetupMode && server.method() == HTTP_GET) {
      handleSetupPage();
    } else if (server.method() == HTTP_OPTIONS) {
      handleCORS();
    } else {
      server.send(404, "text/plain", "Not found");
    }
  });
  
  // Setup mode endpoints
  server.on("/", HTTP_GET, []() {
    if (isInSetupMode) {
      handleSetupPage();
    } else {
      handleRoot();
    }
  });
  server.on("/setup", HTTP_GET, handleSetupPage);
  server.on("/configure", HTTP_POST, handleWiFiConfig);
  
  // Normal mode endpoints
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/status", HTTP_OPTIONS, handleCORS);
  server.on("/api/animation", HTTP_POST, handleAnimation);
  server.on("/api/animation", HTTP_OPTIONS, handleCORS);
  server.on("/api/debug", HTTP_POST, handleDebug);
  server.on("/api/debug", HTTP_OPTIONS, handleCORS);
  server.on("/api/reset", HTTP_POST, handleReset);
  server.on("/api/reset", HTTP_OPTIONS, handleCORS);
  server.on("/api/servo", HTTP_POST, handleServoTest);
  server.on("/api/servo", HTTP_OPTIONS, handleCORS);
  server.on("/wifi", HTTP_GET, handleWiFiSettings);
  server.on("/wifi", HTTP_POST, handleWiFiConfig);
  
  server.begin();
  webServerStarted = true;
  Serial.println("✅ Web server started");
}

void loop() {
  // Check if debug button is pressed
  checkDebugButton();
  
  // Handle DNS server in setup mode
  if (isInSetupMode) {
    dnsServer.processNextRequest();
  }
  
  // Handle WiFi connection (non-blocking, runs in background)
  handleWiFiConnection();

#ifdef TABBIE_MQTT
  // Service the MQTT bridge (non-blocking; only acts once WiFi is up)
  mqttLoop();
#endif

  // Handle web server requests
  server.handleClient();

  // Keep the clock synced and fire any time-of-day scheduled faces (on-device)
  syncTimeIfNeeded();
  checkScheduledFaces();
  logDevSnapshot();

  // Update display animation (always runs, never blocked!)
  updateDisplay();
  
  // Update servo position (smooth movement towards target)
  updateServoMovement();
  
  delay(5);
}

void handleCORS() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
  server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
  server.send(200, "text/plain", "");
}

void handleRoot() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  String html = "<!DOCTYPE html><html><head><title>Tabbie Assistant</title>";
  html += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
  html += "<style>body{font-family:Arial,sans-serif;max-width:900px;margin:28px auto;padding:16px;background:#f6f7f9;color:#17202a;}";
  html += ".status,.panel{background:white;padding:16px;border-radius:10px;margin:14px 0;box-shadow:0 1px 8px rgba(0,0,0,.08);}";
  html += ".grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(135px,1fr));gap:10px;}";
  html += ".button{background:#18212f;color:white;padding:12px 10px;border:none;border-radius:8px;cursor:pointer;font-size:14px;text-align:left;}";
  html += ".button:hover{background:#334155;}.button.active{background:#0f766e;}.button small{display:block;opacity:.72;margin-top:3px;font-size:11px;}";
  html += ".settings{background:#007bff;text-align:center;}.danger{background:#dc3545;text-align:center;}code{font-size:13px;}</style></head><body>";
  html += "<h1>Tabbie Assistant</h1>";
  html += "<div class='status'><h3>Status: Connected!</h3>";
  html += "<p>Current Animation: <strong><span id='current-animation'>Loading...</span></strong></p>";
  html += "<p>Task: <span id='current-task'>-</span></p>";
  html += "<p>MQTT: <span id='mqtt-status'>-</span> · Time: <span id='local-time'>-</span></p>";
  html += "<p>WiFi: " + WiFi.SSID() + "</p>";
  html += "<p>IP: " + WiFi.localIP().toString() + "</p></div>";
  html += "<div class='panel'><h3>Faces</h3><div class='grid' id='faces'>";
  html += "<button class='button' data-face='idle' onclick=\"sendAnimation('idle')\">Idle<small>idle</small></button>";
  html += "<button class='button' data-face='focus' onclick=\"sendAnimation('focus','Focus Session')\">Focus<small>focus</small></button>";
  html += "<button class='button' data-face='break' onclick=\"sendAnimation('break','Break Time')\">Break / Relax<small>break</small></button>";
  html += "<button class='button' data-face='paused' onclick=\"sendAnimation('paused','Paused')\">Angry / Paused<small>paused</small></button>";
  html += "<button class='button' data-face='love' onclick=\"sendAnimation('love','Love')\">Love<small>love</small></button>";
  html += "<button class='button' data-face='pomodoro' onclick=\"sendAnimation('pomodoro','Focus Session')\">Pomodoro<small>pomodoro</small></button>";
  html += "<button class='button' data-face='complete' onclick=\"sendAnimation('complete','Task Done!')\">Complete<small>complete</small></button>";
  html += "<button class='button' data-face='startup' onclick=\"sendAnimation('startup','Hello')\">Startup<small>startup</small></button>";
  html += "<button class='button' data-face='sweat' onclick=\"sendAnimation('sweat','Hot')\">Sweat<small>sweat</small></button>";
  html += "<button class='button' data-face='coffee' onclick=\"sendAnimation('coffee','Coffee Time')\">Coffee<small>coffee</small></button>";
  html += "<button class='button' data-face='mochi_happy' onclick=\"sendAnimation('mochi_happy','Mochi Happy')\">Mochi Happy<small>mochi_happy</small></button>";
  html += "<button class='button' data-face='mochi_angry' onclick=\"sendAnimation('mochi_angry','Mochi Angry')\">Mochi Angry<small>mochi_angry</small></button>";
  html += "<button class='button' data-face='mochi_love' onclick=\"sendAnimation('mochi_love','Mochi Love')\">Mochi Love<small>mochi_love</small></button>";
  html += "<button class='button' data-face='upiir_big_smile' onclick=\"sendAnimation('upiir_big_smile','Big Smile')\">Big Smile<small>upiir_big_smile</small></button>";
  html += "<button class='button' data-face='status_alert' onclick=\"sendAnimation('status_alert','Status Reminder')\">Status Alert<small>status_alert</small></button>";
  html += "</div></div>";
  html += "<div class='panel'><h3>Settings</h3>";
  html += "<button class='button settings' onclick=\"window.location='/wifi'\">WiFi Settings</button>";
  html += "<button class='button danger' onclick=\"resetWiFi()\">Reset WiFi</button></div>";
  html += "<script>";
  html += "async function resetWiFi(){if(confirm('This will clear WiFi settings and restart Tabbie. Continue?')){try{await fetch('/api/reset',{method:'POST'});alert('Tabbie is restarting in setup mode...');}catch(e){alert('Tabbie is restarting...');}}}";
  html += "async function sendAnimation(type,task=''){";
  html += "try{const response=await fetch('/api/animation',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({animation:type,task:task})});";
  html += "if(response.ok){await updateStatus();}}catch(e){console.error('Failed to send animation:',e);}}";
  html += "async function updateStatus(){try{const response=await fetch('/api/status');const data=await response.json();";
  html += "document.getElementById('current-animation').textContent=data.animation||'-';";
  html += "document.getElementById('current-task').textContent=data.task||'-';";
  html += "document.getElementById('mqtt-status').textContent=data.mqttEnabled?(data.mqttConnected?'connected':'disconnected'):'off';";
  html += "document.getElementById('local-time').textContent=data.localTime||'-';";
  html += "document.querySelectorAll('[data-face]').forEach(b=>b.classList.toggle('active',b.dataset.face===data.animation));";
  html += "}catch(e){console.error('Failed to get status:',e);}}";
  html += "setInterval(updateStatus,2000);updateStatus();";
  html += "</script></body></html>";
  
  server.send(200, "text/html", html);
}

void handleSetupPage() {
  String html = "<!DOCTYPE html><html><head><title>Tabbie Setup</title>";
  html += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
  html += "<style>body{font-family:Arial,sans-serif;max-width:400px;margin:50px auto;padding:20px;background:#f5f5f5;}";
  html += ".container{background:white;padding:30px;border-radius:10px;box-shadow:0 2px 10px rgba(0,0,0,0.1);}";
  html += "h1{text-align:center;color:#333;margin-bottom:30px;}";
  html += "input,select{width:100%;padding:12px;margin:10px 0;border:1px solid #ddd;border-radius:5px;font-size:16px;}";
  html += "button{width:100%;background:#007bff;color:white;padding:15px;border:none;border-radius:5px;font-size:16px;cursor:pointer;}";
  html += "button:hover{background:#0056b3;}";
  html += ".error{color:#dc3545;margin:10px 0;padding:10px;background:#f8d7da;border-radius:5px;}";
  html += "</style></head><body><div class='container'>";
  html += "<h1>Tabbie Setup</h1>";
  
  if (lastError.length() > 0) {
    html += "<div class='error'>Error: " + lastError + "</div>";
  }
  
  html += "<form action='/configure' method='POST'>";
  html += "<label>WiFi Network:</label>";
  html += "<select name='ssid' required>";
  
  // Scan for networks
  int networks = WiFi.scanNetworks();
  for (int i = 0; i < networks; i++) {
    html += "<option value='" + WiFi.SSID(i) + "'>" + WiFi.SSID(i) + "</option>";
  }
  
  html += "</select>";
  html += "<label>Password:</label>";
  html += "<input type='password' name='password' placeholder='WiFi Password' required>";
  html += "<button type='submit'>Connect Tabbie</button>";
  html += "</form>";
  html += "<p style='text-align:center;margin-top:20px;font-size:12px;color:#666;'>";
  html += "Tabbie will connect to your WiFi and restart.</p>";
  html += "</div></body></html>";
  
  server.send(200, "text/html", html);
}

void handleWiFiConfig() {
  String ssid = server.arg("ssid");
  String password = server.arg("password");
  
  if (ssid.length() == 0) {
    lastError = "No WiFi network selected";
    handleSetupPage(); // Show setup page with error
    return;
  }
  
  // Save credentials
  preferences.putString("wifi_ssid", ssid);
  preferences.putString("wifi_password", password);
  Serial.print("💾 Saved WiFi credentials: ");
  Serial.println(ssid);
  
  // Update saved credentials and trigger connection
  savedSSID = ssid;
  savedPassword = password;
  
  // Show connection page
  String html = "<!DOCTYPE html><html><head><title>Connecting...</title>";
  html += "<meta http-equiv='refresh' content='10;url=/'>";
  html += "<style>body{font-family:Arial,sans-serif;text-align:center;margin:50px auto;max-width:400px;}";
  html += ".connecting{background:#cce5ff;color:#004085;padding:20px;border-radius:8px;margin:20px 0;}";
  html += "</style></head><body>";
  html += "<h1>Connecting...</h1>";
  html += "<div class='connecting'>";
  html += "<p>Tabbie is connecting to " + ssid + "</p>";
  html += "<p>This page will redirect in 10 seconds.</p>";
  html += "<p>If connection fails, you'll see the setup page again.</p>";
  html += "</div>";
  html += "</body></html>";
  
  server.send(200, "text/html", html);
  
  // Reset WiFi state to trigger fresh connection
  Serial.println("🔄 Restarting WiFi connection...");
  prepareWiFiForRetry(500);
  isInSetupMode = false;
  wifiStatus = "connecting";
  wifiAttemptCount = 0;
  
  Serial.println("📡 WiFi will connect in background...");
}

void handleWiFiSettings() {
  if (isInSetupMode) {
    handleSetupPage();
    return;
  }
  
  String html = "<!DOCTYPE html><html><head><title>Tabbie WiFi Settings</title>";
  html += "<style>body{font-family:Arial,sans-serif;max-width:600px;margin:50px auto;padding:20px;}";
  html += "button{background:#007bff;color:white;padding:10px 20px;border:none;border-radius:5px;margin:5px;cursor:pointer;}";
  html += "button.danger{background:#dc3545;}button:hover{opacity:0.9;}</style></head><body>";
  html += "<h1>Tabbie WiFi Settings</h1>";
  html += "<p><strong>Current Network:</strong> " + WiFi.SSID() + "</p>";
  html += "<p><strong>IP Address:</strong> " + WiFi.localIP().toString() + "</p>";
  html += "<p><strong>Signal Strength:</strong> " + String(WiFi.RSSI()) + " dBm</p>";
  html += "<h3>Actions:</h3>";
  html += "<button onclick=\"if(confirm('Reconfigure WiFi? Tabbie will restart in setup mode.'))window.location='/wifi?action=reset'\">Change WiFi Network</button>";
  html += "<button onclick=\"window.location='/'\">Back to Dashboard</button>";
  html += "</body></html>";
  
  if (server.arg("action") == "reset") {
    // Clear saved credentials and restart in setup mode
    preferences.remove("wifi_ssid");
    preferences.remove("wifi_password");
    
    html = "<!DOCTYPE html><html><head><title>WiFi Reset</title>";
    html += "<meta http-equiv='refresh' content='3;url=/'>";
    html += "</head><body style='font-family:Arial,sans-serif;text-align:center;margin:50px;'>";
    html += "<h1>WiFi Settings Reset</h1>";
    html += "<p>Tabbie is restarting in setup mode...</p>";
    html += "<p>Connect to \"Tabbie-Setup\" to reconfigure.</p></body></html>";
    
    server.send(200, "text/html", html);
    delay(1000);
    ESP.restart();
  }
  
  server.send(200, "text/html", html);
}

void handleStatus() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Content-Type", "application/json");
  
  JsonDocument doc;
  doc["status"] = wifiStatus;
  doc["animation"] = currentAnimation;
  doc["task"] = currentTask;
  doc["uptime"] = millis();
  doc["setupMode"] = isInSetupMode;
  doc["devMode"] = devModeEnabled;
#ifdef TABBIE_MQTT
  doc["mqttEnabled"] = true;
  doc["mqttConnected"] = mqttClient.connected();
  doc["mqttState"] = mqttClient.state();
#else
  doc["mqttEnabled"] = false;
  doc["mqttConnected"] = false;
#endif

  // On-device local time (Europe/Warsaw) so schedules are verifiable
  doc["timeSynced"] = timeSynced;
  if (timeSynced) {
    time_t t = time(nullptr);
    struct tm lt;
    localtime_r(&t, &lt);
    char buf[20];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &lt);
    doc["localTime"] = buf;
  }

  if (!isInSetupMode && WiFi.status() == WL_CONNECTED) {
    doc["ip"] = WiFi.localIP().toString();
    doc["ssid"] = WiFi.SSID();
    doc["rssi"] = WiFi.RSSI();
  } else if (isInSetupMode) {
    doc["ip"] = WiFi.softAPIP().toString();
    doc["connectedDevices"] = WiFi.softAPgetStationNum();
  }
  
  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

void handleReset() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Content-Type", "application/json");
  
  Serial.println("🔄 Resetting WiFi credentials...");
  
  // Clear saved WiFi credentials
  preferences.remove("wifi_ssid");
  preferences.remove("wifi_password");
  
  server.send(200, "application/json", "{\"success\":true,\"message\":\"WiFi credentials cleared. Restarting in setup mode...\"}");
  
  delay(1000);
  ESP.restart();
}

void handleDebug() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Content-Type", "application/json");
  
  // Activate debug mode - shows device info on OLED for 8 seconds
  isDebugMode = true;
  debugModeStartTime = millis();
  
  Serial.println("🔧 Debug mode activated - showing device info on OLED");
  
  JsonDocument response;
  response["success"] = true;
  response["message"] = "Debug info displayed on OLED for 8 seconds";
  response["ip"] = WiFi.localIP().toString();
  response["ssid"] = WiFi.SSID();
  response["rssi"] = WiFi.RSSI();
  response["uptime"] = millis();
  response["animation"] = currentAnimation;
  response["wifiStatus"] = wifiStatus;
  response["mac"] = WiFi.macAddress();
  
  String responseStr;
  serializeJson(response, responseStr);
  server.send(200, "application/json", responseStr);
}

void handleAnimation() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Content-Type", "application/json");
  
  if (server.hasArg("plain")) {
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, server.arg("plain"));
    
    if (error) {
      server.send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
      return;
    }
    
    String newAnimation = doc["animation"];
    String newTask = doc["task"];
    unsigned long durationSeconds = doc["duration"] | 0;
    
    if (newAnimation.length() > 0) {
      currentAnimation = newAnimation;
      currentTask = newTask;
      animationStartTime = millis();
      
      // KEY: Set flag so servo activates immediately on first loop!
      animationTriggeredViaAPI = true;
      idleLoopCount = 0; // Reset loop counter
      
      // Start servo at center
      currentServoPos = SERVO_CENTER;
      targetServoPos = SERVO_CENTER;
      neckServo.write(SERVO_CENTER);
      
      // Focus mode timer
      if (newAnimation == "focus" && durationSeconds > 0) {
        focusStartTime = millis();
        focusDuration = durationSeconds * 1000;
        focusHalfwayDone = false;
      } else {
        focusDuration = 0;
        focusHalfwayDone = false;
      }
      
      // Paused mode timer
      if (newAnimation == "paused") {
        lastPausedShakeTime = millis();
      }
      
      Serial.print("🎬 Animation set: ");
      Serial.print(currentAnimation);
      Serial.println(" (servo will activate on first loop)");
      
      JsonDocument response;
      response["success"] = true;
      response["animation"] = currentAnimation;
      response["task"] = currentTask;
      
      String responseStr;
      serializeJson(response, responseStr);
      server.send(200, "application/json", responseStr);
    } else {
      server.send(400, "application/json", "{\"error\":\"Animation type required\"}");
    }
  } else {
    server.send(400, "application/json", "{\"error\":\"No data received\"}");
  }
}

void handleServoTest() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Content-Type", "application/json");
  
  if (server.hasArg("plain")) {
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, server.arg("plain"));
    
    if (error) {
      server.send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
      return;
    }
    
    int position = SERVO_CENTER;
    
    if (doc.containsKey("position")) {
      if (doc["position"].is<int>()) {
        position = doc["position"].as<int>();
      } else if (doc["position"].is<const char*>()) {
        String posName = doc["position"].as<const char*>();
        if (posName == "left") position = SERVO_LEFT;
        else if (posName == "right") position = SERVO_RIGHT;
        else if (posName == "center") position = SERVO_CENTER;
      }
    }
    
    position = constrain(position, SERVO_LEFT, SERVO_RIGHT);
    
    // Move immediately
    neckServo.write(position);
    currentServoPos = position;
    targetServoPos = position;
    
    Serial.print("🔧 Servo → ");
    Serial.print(position);
    Serial.println("°");
    
    JsonDocument response;
    response["success"] = true;
    response["position"] = position;
    
    String responseStr;
    serializeJson(response, responseStr);
    server.send(200, "application/json", responseStr);
  } else {
    server.send(400, "application/json", "{\"error\":\"No position specified\"}");
  }
}

void setDevMode(bool enabled) {
  bool changed = (devModeEnabled != enabled);
  devModeEnabled = enabled;

  isDebugMode = false;

  if (changed) {
    display.clearBuffer();
    display.sendBuffer();
    lastDevLogAt = 0;
  }

  Serial.print("🔧 Dev mode ");
  Serial.println(enabled ? "ON" : "OFF");
}

void logDevSnapshot() {
  if (!devModeEnabled) return;

  unsigned long now = millis();
  if (lastDevLogAt != 0 && now - lastDevLogAt < 5000) return;
  lastDevLogAt = now;

  Serial.print("DEV status=");
  Serial.print(wifiStatus);
  Serial.print(" ip=");
  Serial.print(WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : "-");
  Serial.print(" rssi=");
  Serial.print(WiFi.status() == WL_CONNECTED ? String(WiFi.RSSI()) : "n/a");
#ifdef TABBIE_MQTT
  Serial.print(" mqtt=");
  Serial.print(mqttClient.connected() ? "ok" : "fail");
  Serial.print(" mqttState=");
  Serial.print(mqttClient.state());
#else
  Serial.print(" mqtt=off");
#endif
  Serial.print(" anim=");
  Serial.print(currentAnimation);
  Serial.print(" task=");
  Serial.print(currentTask);
  Serial.print(" heap=");
  Serial.println(ESP.getFreeHeap());
}

#ifdef TABBIE_MQTT
// Apply an animation command from the MQTT bridge. Mirrors handleAnimation().
void applyAnimation(const String& anim, const String& task, unsigned long durSec) {
  triggerAnimation(anim, task, durSec);
}

bool applyDevCommand(const String& value) {
  String normalized = value;
  normalized.trim();
  normalized.toLowerCase();

  if (normalized == "toggle") {
    setDevMode(!devModeEnabled);
    return true;
  }
  if (normalized == "1" || normalized == "true" || normalized == "on" || normalized == "enable" || normalized == "enabled") {
    setDevMode(true);
    return true;
  }
  if (normalized == "0" || normalized == "false" || normalized == "off" || normalized == "disable" || normalized == "disabled") {
    setDevMode(false);
    return true;
  }
  return false;
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  // Accept JSON {"animation":"..","task":"..","duration":N},
  // JSON {"dev":true|false|"toggle"}, or a bare name string.
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, payload, length);
  if (!err) {
    if (doc["dev"].is<bool>()) {
      setDevMode(doc["dev"].as<bool>());
      mqttClient.publish(MQTT_STATUS_TOPIC, devModeEnabled ? "dev:on" : "dev:off", true);
      return;
    }
    if (doc["dev"].is<const char*>()) {
      if (applyDevCommand(doc["dev"].as<const char*>())) {
        mqttClient.publish(MQTT_STATUS_TOPIC, devModeEnabled ? "dev:on" : "dev:off", true);
        return;
      }
    }

    String anim = doc["animation"] | "";
    String task = doc["task"] | "";
    unsigned long dur = doc["duration"] | 0UL;
    if (anim.length()) {
      applyAnimation(anim, task, dur);
      Serial.print("📩 MQTT cmd: "); Serial.println(anim);
    }
  } else {
    String raw;
    for (unsigned int i = 0; i < length; i++) raw += (char)payload[i];
    raw.trim();
    if (raw.length()) {
      String rawLower = raw;
      rawLower.toLowerCase();
      if (rawLower == "dev" || rawLower == "dev toggle") {
        setDevMode(!devModeEnabled);
        mqttClient.publish(MQTT_STATUS_TOPIC, devModeEnabled ? "dev:on" : "dev:off", true);
        Serial.println("📩 MQTT dev toggle");
        return;
      }
      if (rawLower == "dev on" || rawLower == "dev true" || rawLower == "dev enable") {
        setDevMode(true);
        mqttClient.publish(MQTT_STATUS_TOPIC, "dev:on", true);
        Serial.println("📩 MQTT dev on");
        return;
      }
      if (rawLower == "dev off" || rawLower == "dev false" || rawLower == "dev disable") {
        setDevMode(false);
        mqttClient.publish(MQTT_STATUS_TOPIC, "dev:off", true);
        Serial.println("📩 MQTT dev off");
        return;
      }

      applyAnimation(raw, "mqtt", 0);
      Serial.print("📩 MQTT cmd (raw): "); Serial.println(raw);
    }
  }
}

void mqttLoop() {
  if (WiFi.status() != WL_CONNECTED) return;
  if (!mqttClient.connected()) {
    unsigned long now = millis();
    if (now - lastMqttReconnect < 5000) return;   // throttle reconnects
    lastMqttReconnect = now;
    bool ok;
#if defined(MQTT_USER)
    ok = mqttClient.connect(MQTT_CLIENT_ID, MQTT_USER, MQTT_PASS);
#else
    ok = mqttClient.connect(MQTT_CLIENT_ID);
#endif
    if (ok) {
      mqttClient.subscribe(MQTT_CMD_TOPIC);
      mqttClient.publish(MQTT_STATUS_TOPIC, "online", true);
      Serial.println("✅ MQTT connected");
    } else {
      Serial.print("❌ MQTT connect failed, rc="); Serial.println(mqttClient.state());
    }
    return;
  }
  mqttClient.loop();
}
#endif  // TABBIE_MQTT

void drawCoffeeAnimation() {
  static int frame = 0;
  static unsigned long lastFrameTime = 0;
  static unsigned long lastStart = 0;

  // Reset on animation start
  if (animationStartTime != lastStart) {
    frame = 0;
    lastFrameTime = 0;
    lastStart = animationStartTime;
  }

  unsigned long now = millis();
  if (now - lastFrameTime < COFFEE01_FRAME_DELAY) return;
  lastFrameTime = now;

  // Draw frame
  display.clearBuffer();
  const uint8_t* frameData = (const uint8_t*)pgm_read_ptr(&coffee01_frames[frame]);
  display.drawBitmap(0, 0, 128 / 8, 64, frameData);
  flushDisplay();

  frame++;
  if (frame >= COFFEE01_FRAME_COUNT) frame = 0;
}

// Set the current animation from any source (API, MQTT, or the on-device
// scheduler). Single source of truth; works whether or not MQTT is compiled in.
void triggerAnimation(const String& anim, const String& task, unsigned long durSec) {
  if (anim.length() == 0) return;
  currentAnimation = anim;
  currentTask = task;
  animationStartTime = millis();
  animationTriggeredViaAPI = true;
  idleLoopCount = 0;
  currentServoPos = SERVO_CENTER;
  targetServoPos = SERVO_CENTER;
  neckServo.write(SERVO_CENTER);
  if (anim == "focus" && durSec > 0) {
    focusStartTime = millis();
    focusDuration = durSec * 1000;
    focusHalfwayDone = false;
  } else {
    focusDuration = 0;
    focusHalfwayDone = false;
  }
  if (anim == "paused") {
    lastPausedShakeTime = millis();
  }
}

// Keep the device clock in sync via NTP, in Europe/Warsaw local time.
// Non-blocking: requests once WiFi is up, then polls until the clock is valid.
void syncTimeIfNeeded() {
  if (timeSynced) return;
  if (WiFi.status() != WL_CONNECTED) return;

  unsigned long now = millis();
  if (!timeConfigStarted) {
    configTzTime(TABBIE_TZ, "pool.ntp.org", "time.google.com", "time.nist.gov");
    timeConfigStarted = true;
    lastTimeSyncAttempt = now;
    Serial.println("🕒 NTP time sync requested (Europe/Warsaw)...");
    return;
  }
  if (now - lastTimeSyncAttempt < 2000) return;   // poll every 2s
  lastTimeSyncAttempt = now;

  time_t t = time(nullptr);
  if (t > 1700000000) {   // ~2023-11 onwards => clock is real, not the boot epoch
    timeSynced = true;
    struct tm lt;
    localtime_r(&t, &lt);
    Serial.printf("🕒 Time synced: %04d-%02d-%02d %02d:%02d:%02d local\n",
                  lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday,
                  lt.tm_hour, lt.tm_min, lt.tm_sec);
  }
}

// Fire time-of-day faces from the scheduledFaces table, entirely on-device.
void checkScheduledFaces() {
  if (!timeSynced || isInSetupMode || !hasCompletedStartup) return;
  unsigned long now = millis();

  // Auto-revert a scheduled face to idle after its hold window — but only if
  // nothing else (API/MQTT) has changed the animation since we set it.
  if (scheduledRevertAt != 0 && now >= scheduledRevertAt) {
    if (currentAnimation == scheduledActiveAnim && animationStartTime == scheduledActiveStart) {
      triggerAnimation("idle", "", 0);
    }
    scheduledRevertAt = 0;
  }

  if (now - lastScheduleCheck < 15000) return;   // ~4 checks/min is plenty
  lastScheduleCheck = now;

  time_t t = time(nullptr);
  struct tm lt;
  localtime_r(&t, &lt);
  int ymd = (lt.tm_year + 1900) * 10000 + (lt.tm_mon + 1) * 100 + lt.tm_mday;

  for (int i = 0; i < scheduledFaceCount; i++) {
    ScheduledFace &s = scheduledFaces[i];
    if (lt.tm_hour == s.hour && lt.tm_min == s.minute && s.lastFiredYmd != ymd) {
      s.lastFiredYmd = ymd;
      triggerAnimation(s.animation, s.task, 0);
      scheduledActiveAnim = s.animation;
      scheduledActiveStart = animationStartTime;
      scheduledRevertAt = (s.showSec > 0) ? now + (unsigned long)s.showSec * 1000UL : 0;
      Serial.printf("⏰ Scheduled face '%s' fired at %02d:%02d\n",
                    s.animation, lt.tm_hour, lt.tm_min);
    }
  }
}

void drawMochi_happyAnimation() {
  static int frame = 0;
  static unsigned long lastFrameTime = 0;
  static unsigned long lastStart = 0;

  // Reset on animation start
  if (animationStartTime != lastStart) {
    frame = 0;
    lastFrameTime = 0;
    lastStart = animationStartTime;
  }

  unsigned long now = millis();
  if (now - lastFrameTime < MOCHI_HAPPY01_FRAME_DELAY) return;
  lastFrameTime = now;

  // Draw frame
  display.clearBuffer();
  const uint8_t* frameData = (const uint8_t*)pgm_read_ptr(&mochi_happy01_frames[frame]);
  display.drawBitmap(0, 0, 128 / 8, 64, frameData);
  flushDisplay();

  frame++;
  if (frame >= MOCHI_HAPPY01_FRAME_COUNT) frame = 0;
}

void drawMochi_angryAnimation() {
  static int frame = 0;
  static unsigned long lastFrameTime = 0;
  static unsigned long lastStart = 0;

  // Reset on animation start
  if (animationStartTime != lastStart) {
    frame = 0;
    lastFrameTime = 0;
    lastStart = animationStartTime;
  }

  unsigned long now = millis();
  if (now - lastFrameTime < MOCHI_ANGRY01_FRAME_DELAY) return;
  lastFrameTime = now;

  // Draw frame
  display.clearBuffer();
  const uint8_t* frameData = (const uint8_t*)pgm_read_ptr(&mochi_angry01_frames[frame]);
  display.drawBitmap(0, 0, 128 / 8, 64, frameData);
  flushDisplay();

  frame++;
  if (frame >= MOCHI_ANGRY01_FRAME_COUNT) frame = 0;
}

void drawMochi_loveAnimation() {
  static int frame = 0;
  static unsigned long lastFrameTime = 0;
  static unsigned long lastStart = 0;

  // Reset on animation start
  if (animationStartTime != lastStart) {
    frame = 0;
    lastFrameTime = 0;
    lastStart = animationStartTime;
  }

  unsigned long now = millis();
  if (now - lastFrameTime < MOCHI_LOVE01_FRAME_DELAY) return;
  lastFrameTime = now;

  // Draw frame
  display.clearBuffer();
  const uint8_t* frameData = (const uint8_t*)pgm_read_ptr(&mochi_love01_frames[frame]);
  display.drawBitmap(0, 0, 128 / 8, 64, frameData);
  flushDisplay();

  frame++;
  if (frame >= MOCHI_LOVE01_FRAME_COUNT) frame = 0;
}

void drawUpiir_big_smileAnimation() {
  static int frame = 0;
  static unsigned long lastFrameTime = 0;
  static unsigned long lastStart = 0;

  // Reset on animation start
  if (animationStartTime != lastStart) {
    frame = 0;
    lastFrameTime = 0;
    lastStart = animationStartTime;
  }

  unsigned long now = millis();
  if (now - lastFrameTime < UPIIR_BIG_SMILE01_FRAME_DELAY) return;
  lastFrameTime = now;

  // Draw frame
  display.clearBuffer();
  const uint8_t* frameData = (const uint8_t*)pgm_read_ptr(&upiir_big_smile01_frames[frame]);
  display.drawBitmap(0, 0, 128 / 8, 64, frameData);
  flushDisplay();

  frame++;
  if (frame >= UPIIR_BIG_SMILE01_FRAME_COUNT) frame = 0;
}

void drawStatus_alertAnimation() {
  static int frame = 0;
  static unsigned long lastFrameTime = 0;
  static unsigned long lastStart = 0;

  // Reset on animation start
  if (animationStartTime != lastStart) {
    frame = 0;
    lastFrameTime = 0;
    lastStart = animationStartTime;
  }

  unsigned long now = millis();
  if (now - lastFrameTime < 100) return;
  lastFrameTime = now;

  display.clearBuffer();

  display.setFont(u8g2_font_6x10_tf);
  if (frame % 8 < 5) {
    display.drawStr(2, 9, "STATUS STATUS");
    display.drawStr(18, 62, "STATUS!");
  } else {
    display.drawStr(20, 9, "WRITE STATUS");
    display.drawStr(8, 62, "NOW NOW NOW");
  }

  int ax = 64, ay = 14, bx = 42, by = 48, cx = 86, cy = 48;
  switch ((frame / 6) % 4) {
    case 1: ax = 84; ay = 32; bx = 50; by = 12; cx = 50; cy = 52; break;
    case 2: ax = 64; ay = 50; bx = 42; by = 16; cx = 86; cy = 16; break;
    case 3: ax = 44; ay = 32; bx = 78; by = 12; cx = 78; cy = 52; break;
  }
  display.drawLine(ax, ay, bx, by);
  display.drawLine(bx, by, cx, cy);
  display.drawLine(cx, cy, ax, ay);
  display.drawLine((ax + 64) / 2, (ay + 32) / 2, (bx + 64) / 2, (by + 32) / 2);
  display.drawLine((bx + 64) / 2, (by + 32) / 2, (cx + 64) / 2, (cy + 32) / 2);
  display.drawLine((cx + 64) / 2, (cy + 32) / 2, (ax + 64) / 2, (ay + 32) / 2);

  display.drawBox(62, 22, 5, 17);
  display.drawBox(62, 42, 5, 4);

  int phase = frame % 8;
  if (phase == 0 || phase == 4) {
    display.drawLine(64, 0, 64, 8);
    display.drawLine(64, 56, 64, 63);
  } else if (phase == 1 || phase == 5) {
    display.drawLine(91, 5, 84, 12);
    display.drawLine(37, 59, 44, 52);
  } else if (phase == 2 || phase == 6) {
    display.drawLine(119, 32, 127, 32);
    display.drawLine(0, 32, 8, 32);
  } else {
    display.drawLine(91, 59, 84, 52);
    display.drawLine(37, 5, 44, 12);
  }

  if (frame % 6 < 3) {
    display.drawFrame(0, 23, 10, 19);
    display.drawFrame(118, 23, 10, 19);
  } else {
    display.drawBox(2, 25, 6, 15);
    display.drawBox(120, 25, 6, 15);
  }
  flushDisplay();

  frame++;
  if (frame >= 24) frame = 0;
}

void updateDisplay() {
  // Handle startup animation - play once then go to idle
  if (!hasCompletedStartup) {
    drawStartupAnimation();
    return;
  }
  
  // In setup mode, show setup screen
  if (isInSetupMode) {
    drawSetupMode();
    return;
  }

  // DevMode is a persistent internal diagnostics screen controlled over MQTT.
  if (devModeEnabled) {
    drawDevInfo();
    return;
  }

  // Handle debug mode - show device info temporarily
  if (isDebugMode) {
    if (millis() - debugModeStartTime < DEBUG_MODE_DURATION) {
      drawDebugInfo();
      return;
    } else {
      // Debug mode expired, return to normal
      isDebugMode = false;
      Serial.println("🔧 Debug mode ended - returning to normal display");
    }
  }
  
  // Otherwise, always show animations - WiFi connection happens in background
  if (currentAnimation == "idle") {
    drawIdleAnimation();
  } else if (currentAnimation == "focus") {
    drawFocusAnimation();
  } else if (currentAnimation == "break") {
    drawRelaxAnimation();
  } else if (currentAnimation == "paused") {
    drawAngryAnimation();
  } else if (currentAnimation == "love") {
    drawLoveAnimation();
  } else if (currentAnimation == "sweat") {
    drawSweatAnimation();
  } else if (currentAnimation == "coffee") {
    drawCoffeeAnimation();
  } else if (currentAnimation == "mochi_happy") {
    drawMochi_happyAnimation();
  } else if (currentAnimation == "mochi_angry") {
    drawMochi_angryAnimation();
  } else if (currentAnimation == "mochi_love") {
    drawMochi_loveAnimation();
  } else if (currentAnimation == "upiir_big_smile") {
    drawUpiir_big_smileAnimation();
  } else if (currentAnimation == "status_alert") {
    drawStatus_alertAnimation();
  } else if (currentAnimation == "pomodoro") {
    drawPomodoroAnimation();
  } else if (currentAnimation == "complete") {
    drawTaskCompleteAnimation();
  }
}

void drawStatusMark(int x, int y, bool ok) {
  if (ok) {
    display.drawLine(x, y + 4, x + 1, y + 6);
    display.drawLine(x + 1, y + 6, x + 4, y + 1);
  } else {
    display.drawLine(x, y + 1, x + 4, y + 5);
    display.drawLine(x + 4, y + 1, x, y + 5);
  }
}

void drawStatusOverlay() {
  if (!devModeEnabled || !hasCompletedStartup || isDebugMode || isInSetupMode) return;

  bool wifiOk = (WiFi.status() == WL_CONNECTED && wifiStatus == "connected");
#ifdef TABBIE_MQTT
  bool mqttOk = mqttClient.connected();
#else
  bool mqttOk = false;
#endif

  display.setDrawColor(0);
  display.drawBox(96, 0, 32, 9);
  display.setDrawColor(1);
  display.setFont(u8g2_font_5x7_tf);

  display.drawStr(97, 7, "W");
  drawStatusMark(105, 1, wifiOk);
  display.drawStr(114, 7, "M");
  drawStatusMark(123, 1, mqttOk);
}

String clipForDisplay(const String& value, int maxLen) {
  if (value.length() <= maxLen) return value;
  if (maxLen <= 3) return value.substring(0, maxLen);
  return value.substring(0, maxLen - 3) + "...";
}

void flushDisplay(bool showOverlay) {
  if (showOverlay) {
    drawStatusOverlay();
  }
  display.sendBuffer();
}

void checkDebugButton() {
  // Don't check button during startup (first 5 seconds) to avoid false triggers
  if (millis() < 5000) return;
  
  // Don't trigger while startup animation is still playing
  if (!hasCompletedStartup) return;
  
  // Don't allow re-triggering while debug mode is active
  if (isDebugMode) return;
  
  // Check if button is pressed (LOW because of INPUT_PULLUP)
  if (digitalRead(DEBUG_BUTTON_PIN) == LOW) {
    // Debounce check
    if (millis() - lastButtonPress > BUTTON_DEBOUNCE_MS) {
      lastButtonPress = millis();
      
      // Activate debug mode
      isDebugMode = true;
      debugModeStartTime = millis();
      Serial.println("🔘 Debug button pressed - showing device info");
    }
  }
}

void drawDebugInfo() {
  display.clearBuffer();
  display.setFont(u8g2_font_6x10_tf);
  
  // Show different info based on connection state
  if (isInSetupMode) {
    // Setup mode - show setup instructions
    display.drawStr(0, 10, "=== SETUP MODE ===");
    display.drawStr(0, 24, "Connect to WiFi:");
    display.drawStr(0, 36, "  Tabbie-Setup");
    display.drawStr(0, 48, "Then visit:");
    display.drawStr(0, 60, "  192.168.4.1");
  } else if (wifiStatus == "connecting") {
    // Connecting - show status
    display.drawStr(0, 10, "=== CONNECTING ===");
    display.drawStr(0, 26, "WiFi:");
    String ssidShort = savedSSID.substring(0, 15);
    display.drawStr(36, 26, ssidShort.c_str());
    display.drawStr(0, 42, "Please wait...");
    
    // Show attempt count
    String attempts = "Attempt " + String(wifiAttemptCount) + "/" + String(MAX_WIFI_ATTEMPTS);
    display.drawStr(0, 58, attempts.c_str());
  } else if (wifiStatus == "connected") {
    display.drawStr(0, 9, "=== DEVICE STATUS ===");

    String ipDisplay = WiFi.localIP().toString();
    display.drawStr(0, 21, ipDisplay.c_str());

    String ssidDisplay = WiFi.SSID();
    if (ssidDisplay.length() > 18) {
      ssidDisplay = ssidDisplay.substring(0, 15) + "...";
    }
    display.drawStr(0, 33, ssidDisplay.c_str());

    int rssi = WiFi.RSSI();
    String signal = "WIFI OK " + String(rssi) + "dBm";
    if (signal.length() > 21) signal = signal.substring(0, 21);
    display.drawStr(0, 45, signal.c_str());

#ifdef TABBIE_MQTT
    String mqtt = mqttClient.connected()
      ? "MQTT OK /api/status"
      : "MQTT FAIL state " + String(mqttClient.state());
#else
    String mqtt = "MQTT OFF /api/status";
#endif
    if (mqtt.length() > 19) mqtt = mqtt.substring(0, 19);
    display.drawStr(0, 57, mqtt.c_str());

    int secondsLeft = (DEBUG_MODE_DURATION - (millis() - debugModeStartTime)) / 1000;
    String countdown = String(secondsLeft) + "s";
    display.drawStr(110, 57, countdown.c_str());
  } else {
    // Failed/disconnected
    display.drawStr(0, 10, "=== WIFI ERROR ===");
    display.drawStr(0, 26, "Not connected!");
    
    if (lastError.length() > 0) {
      String errShort = lastError.substring(0, 20);
      display.drawStr(0, 40, errShort.c_str());
    }
    
    display.drawStr(0, 56, "Check WiFi settings");
  }
  
  flushDisplay(false);
}

void drawDevInfo() {
  display.clearBuffer();
  display.setFont(u8g2_font_5x7_tf);

  unsigned long now = millis();
  int page = (now / 3000) % 3;

  if (page == 0) {
    display.drawStr(0, 7, "DEV 1/3 NET");

    String wifi = "W " + wifiStatus;
    display.drawStr(0, 17, clipForDisplay(wifi, 21).c_str());

    String ip = WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : "-";
    display.drawStr(0, 27, clipForDisplay("IP " + ip, 21).c_str());

    String rssi = WiFi.status() == WL_CONNECTED ? String(WiFi.RSSI()) + "dBm" : "n/a";
    display.drawStr(0, 37, clipForDisplay("RSSI " + rssi, 21).c_str());

#ifdef TABBIE_MQTT
    String mqtt = mqttClient.connected() ? "M OK" : "M FAIL " + String(mqttClient.state());
#else
    String mqtt = "M OFF";
#endif
    display.drawStr(0, 47, clipForDisplay(mqtt, 21).c_str());
    display.drawStr(0, 57, clipForDisplay("SSID " + WiFi.SSID(), 21).c_str());
  } else if (page == 1) {
    display.drawStr(0, 7, "DEV 2/3 FACE");

    display.drawStr(0, 17, clipForDisplay("ANIM " + currentAnimation, 21).c_str());
    display.drawStr(0, 27, clipForDisplay("TASK " + currentTask, 21).c_str());

    String age = "AGE " + String((now - animationStartTime) / 1000) + "s";
    age += animationTriggeredViaAPI ? " API" : " AUTO";
    display.drawStr(0, 37, clipForDisplay(age, 21).c_str());

    String focus = "FOCUS ";
    if (focusDuration > 0) {
      unsigned long elapsed = min(now - focusStartTime, focusDuration) / 1000;
      focus += String(elapsed) + "/" + String(focusDuration / 1000) + "s";
    } else {
      focus += "-";
    }
    display.drawStr(0, 47, clipForDisplay(focus, 21).c_str());

    String sched = "SCHED ";
    if (scheduledRevertAt != 0) {
      unsigned long left = scheduledRevertAt > now ? (scheduledRevertAt - now) / 1000 : 0;
      sched += scheduledActiveAnim + " " + String(left) + "s";
    } else {
      sched += "-";
    }
    display.drawStr(0, 57, clipForDisplay(sched, 21).c_str());
  } else {
    display.drawStr(0, 7, "DEV 3/3 SYS");

    display.drawStr(0, 17, clipForDisplay("UP " + String(now / 1000) + "s", 21).c_str());
    display.drawStr(0, 27, clipForDisplay("HEAP " + String(ESP.getFreeHeap()), 21).c_str());

    String clock = "CLK ";
    if (timeSynced) {
      time_t t = time(nullptr);
      struct tm lt;
      localtime_r(&t, &lt);
      char buf[12];
      strftime(buf, sizeof(buf), "%H:%M:%S", &lt);
      clock += buf;
    } else {
      clock += "sync...";
    }
    display.drawStr(0, 37, clipForDisplay(clock, 21).c_str());

    String servo = "SERVO " + String(currentServoPos) + ">" + String(targetServoPos);
    display.drawStr(0, 47, clipForDisplay(servo, 21).c_str());

    String err = lastError.length() ? "ERR " + lastError : "ERR -";
    display.drawStr(0, 57, clipForDisplay(err, 21).c_str());
  }

  flushDisplay(false);
}

void drawSetupMode() {
  static int frame = 0;
  frame++;
  
  display.clearBuffer();
  display.setFont(u8g2_font_6x10_tf);
  
  int y = 10;
  if (lastError.length() > 0) {
    display.drawStr(0, y, "WiFi Error!");
    y += 12;
  } else {
    display.drawStr(0, y, "WiFi Setup");
    y += 12;
  }
  
  y += 2;
  display.drawStr(0, y, "1. Connect to WiFi:");
  y += 10;
  display.drawStr(0, y, "   Tabbie-Setup");
  y += 12;
  display.drawStr(0, y, "2. Visit:");
  y += 10;
  display.drawStr(0, y, "   192.168.4.1");
  
  // Blinking indicator
  if ((frame / 10) % 2 == 0) {
    display.drawPixel(125, 2);
    display.drawPixel(126, 2);
    display.drawPixel(127, 2);
  }
  
  flushDisplay(false);
}

void drawConnecting() {
  static int frame = 0;
  frame++;
  
  display.clearBuffer();
  display.setFont(u8g2_font_6x10_tf);
  
  display.drawStr(0, 10, "Connecting...");
  
  String savedSSID = preferences.getString("wifi_ssid", "");
  if (savedSSID.length() > 15) {
    savedSSID = savedSSID.substring(0, 12) + "...";
  }
  display.drawStr(0, 24, savedSSID.c_str());
  
  // Animated dots
  String dots = "";
  for (int i = 0; i < (frame / 5) % 4; i++) {
    dots += ".";
  }
  display.drawStr(0, 40, dots.c_str());
  
  flushDisplay(false);
}

void drawConnected() {
  display.clearBuffer();
  display.setFont(u8g2_font_6x10_tf);
  
  display.drawStr(0, 10, "Connected!");
  display.drawStr(0, 24, WiFi.SSID().c_str());
  display.drawStr(0, 38, WiFi.localIP().toString().c_str());
  
  flushDisplay(false);
}

void drawError() {
  static int frame = 0;
  frame++;
  
  display.clearBuffer();
  display.setFont(u8g2_font_6x10_tf);
  
  display.drawStr(0, 10, "WiFi Error!");
  
  int y = 24;
  if (lastError.length() > 0) {
    String error = lastError;
    if (error.length() > 21) {
      error = error.substring(0, 18) + "...";
    }
    display.drawStr(0, y, error.c_str());
  } else {
    display.drawStr(0, y, "Check WiFi config");
  }
  
  display.drawStr(0, 48, "Restarting...");
  
  // Blinking error indicator
  if ((frame / 8) % 2 == 0) {
    display.drawDisc(5, 5, 2);
  }
  
  flushDisplay(false);
}

void drawIdleAnimation() {
  static int frame = 0;
  static unsigned long lastFrameTime = 0;
  static unsigned long lastStart = 0;
  static bool servoActive = false;
  
  // Reset when animation restarts
  if (animationStartTime != lastStart) {
    frame = 0;
    lastFrameTime = 0;
    lastStart = animationStartTime;
    
    // If triggered via API, activate servo immediately!
    if (animationTriggeredViaAPI) {
      servoActive = true;
      Serial.println("🔄 Idle started (API) - servo ACTIVE first loop");
    } else {
      servoActive = false;
    }
  }
  
  unsigned long now = millis();
  if (now - lastFrameTime < IDLE01_FRAME_DELAY) return;
  lastFrameTime = now;
  
  // Draw animation frame
  display.clearBuffer();
  const uint8_t* frameData = (const uint8_t*)pgm_read_ptr(&idle01_frames[frame]);
  display.drawBitmap(0, 0, 128 / 8, 64, frameData);
  flushDisplay();
  
  // Servo keyframes (when active)
  // Idle keyframes: frame 25=left, 50=center, 75=right, 90=center
  if (servoActive) {
    if (frame == 25) moveServoTo(SERVO_LEFT);
    else if (frame == 50) moveServoTo(SERVO_CENTER);
    else if (frame == 75) moveServoTo(SERVO_RIGHT);
    else if (frame == 90) moveServoTo(SERVO_CENTER);
  }
  
  // Next frame
  frame++;
  if (frame >= IDLE01_FRAME_COUNT) {
    frame = 0;
    idleLoopCount++;
    
    // After first loop, clear API flag
    if (animationTriggeredViaAPI) {
      animationTriggeredViaAPI = false;
      servoActive = false; // Next loops follow normal pattern
      Serial.println("🔄 First loop done - returning to normal (every 4th loop)");
    }
    
    // Normal mode: activate every 4th loop
    if (!animationTriggeredViaAPI && idleLoopCount % IDLE_SERVO_EVERY_N_LOOPS == 0) {
      servoActive = true;
      Serial.println("🔄 Idle loop " + String(idleLoopCount) + " - servo active");
    } else if (!animationTriggeredViaAPI) {
      servoActive = false;
    }
  }
}

void drawFocusAnimation() {
  static int frame = 0;
  static unsigned long lastFrameTime = 0;
  static unsigned long lastStart = 0;
  static int lastMilestoneShown = 0; // 0=none, 1=25%, 2=50%, 3=75%
  static unsigned long milestoneShowTime = 0;
  const unsigned long MILESTONE_DISPLAY_MS = 3000; // Show milestone for 3 seconds
  
  // Reset on animation start
  if (animationStartTime != lastStart) {
    frame = 0;
    lastFrameTime = 0;
    lastStart = animationStartTime;
    lastMilestoneShown = 0;
    milestoneShowTime = 0;
    focusHalfwayDone = false;
  }
  
  unsigned long now = millis();
  
  // Calculate progress (handle paused state by using focusStartTime which doesn't change when paused)
  unsigned long elapsed = 0;
  float progress = 0.0f;
  int currentMilestone = 0;
  
  if (focusDuration > 0) {
    elapsed = now - focusStartTime;
    progress = min((float)elapsed / (float)focusDuration, 1.0f);
    
    // Determine which milestone we've reached
    if (progress >= 0.75f) currentMilestone = 3;
    else if (progress >= 0.50f) currentMilestone = 2;
    else if (progress >= 0.25f) currentMilestone = 1;
  }
  
  // Check if we should show a new milestone
  if (currentMilestone > lastMilestoneShown && focusDuration > 0) {
    lastMilestoneShown = currentMilestone;
    milestoneShowTime = now;
    
    // Servo nudge at 50% milestone
    if (currentMilestone == 2 && !focusHalfwayDone) {
      focusHalfwayDone = true;
      moveServoTo(SERVO_LEFT);
      Serial.println("🎯 Focus 50% - servo nudge");
    }
  }
  
  // Return servo to center after nudge
  if (focusHalfwayDone && now - milestoneShowTime > 500 && lastMilestoneShown == 2) {
    moveServoTo(SERVO_CENTER);
  }
  
  // Are we currently showing a milestone overlay?
  bool showingMilestone = (milestoneShowTime > 0 && now - milestoneShowTime < MILESTONE_DISPLAY_MS);
  
  display.clearBuffer();
  
  if (showingMilestone && focusDuration > 0) {
    // ========== MILESTONE OVERLAY (no animation visible) ==========
    unsigned long remaining = focusDuration > elapsed ? focusDuration - elapsed : 0;
    int remainingMin = remaining / 60000;
    int remainingSec = (remaining % 60000) / 1000;
    
    // Milestone message at top
    display.setFont(u8g2_font_6x10_tf);
    const char* message = "";
    switch (lastMilestoneShown) {
      case 1: message = ">> 25% done >>"; break;
      case 2: message = ">>> Halfway! >>>"; break;
      case 3: message = ">>>> Almost! >>>>"; break;
    }
    int msgWidth = strlen(message) * 6;
    display.drawStr((128 - msgWidth) / 2, 16, message);
    
    // Progress bar (centered, larger)
    int barWidth = (int)(progress * 96);
    display.drawFrame(16, 26, 96, 12);
    if (barWidth > 2) display.drawBox(18, 28, barWidth - 4, 8);
    
    // Time remaining at bottom
    char timeStr[24];
    sprintf(timeStr, "%d:%02d left", remainingMin, remainingSec);
    int timeWidth = strlen(timeStr) * 6;
    display.drawStr((128 - timeWidth) / 2, 52, timeStr);
    
    Serial.printf("📊 Milestone %d shown: %s (%d:%02d remaining)\n", 
                  lastMilestoneShown, message, remainingMin, remainingSec);
    
  } else {
    // ========== NORMAL ANIMATION ==========
    if (now - lastFrameTime >= FOCUS01_FRAME_DELAY) {
      lastFrameTime = now;
      frame++;
      if (frame >= FOCUS01_FRAME_COUNT) frame = 0;
    }
    
    const uint8_t* frameData = (const uint8_t*)pgm_read_ptr(&focus01_frames[frame]);
    display.drawBitmap(0, 0, 128 / 8, 64, frameData);
  }
  
  flushDisplay();
}

void drawRelaxAnimation() {
  static int frame = 0;
  static unsigned long lastFrameTime = 0;
  static unsigned long lastStart = 0;
  
  // Reset on animation start
  if (animationStartTime != lastStart) {
    frame = 0;
    lastFrameTime = 0;
    lastStart = animationStartTime;
    Serial.println("🔄 Break animation started - servo active every loop");
  }
  
  unsigned long now = millis();
  if (now - lastFrameTime < RELAX01_FRAME_DELAY) return;
  lastFrameTime = now;
  
  // Draw frame
  display.clearBuffer();
  const uint8_t* frameData = (const uint8_t*)pgm_read_ptr(&relax01_frames[frame]);
  display.drawBitmap(0, 0, 128 / 8, 64, frameData);
  flushDisplay();
  
  // Servo keyframes: 40=left, 50=right, 60=center (every loop)
  if (frame == 40) moveServoTo(SERVO_LEFT);
  else if (frame == 50) moveServoTo(SERVO_RIGHT);
  else if (frame == 60) moveServoTo(SERVO_CENTER);
  
  frame++;
  if (frame >= RELAX01_FRAME_COUNT) frame = 0;
}

void drawSweatAnimation() {
  static int frame = 0;
  static unsigned long lastFrameTime = 0;
  static unsigned long lastStart = 0;

  // Reset on animation start
  if (animationStartTime != lastStart) {
    frame = 0;
    lastFrameTime = 0;
    lastStart = animationStartTime;
  }

  unsigned long now = millis();
  if (now - lastFrameTime < SWEAT01_FRAME_DELAY) return;
  lastFrameTime = now;

  // Draw frame
  display.clearBuffer();
  const uint8_t* frameData = (const uint8_t*)pgm_read_ptr(&sweat01_frames[frame]);
  display.drawBitmap(0, 0, 128 / 8, 64, frameData);
  flushDisplay();

  frame++;
  if (frame >= SWEAT01_FRAME_COUNT) frame = 0;
}

void drawLoveAnimation() {
  static int frame = 0;
  static unsigned long lastFrameTime = 0;
  static unsigned long lastStart = 0;

  // Reset on animation start
  if (animationStartTime != lastStart) {
    frame = 0;
    lastFrameTime = 0;
    lastStart = animationStartTime;
    Serial.println("💕 Love animation started - servo wiggle!");
  }
  
  unsigned long now = millis();
  if (now - lastFrameTime < LOVE01_FRAME_DELAY) return;
  lastFrameTime = now;
  
  // Draw frame
  display.clearBuffer();
  const uint8_t* frameData = (const uint8_t*)pgm_read_ptr(&love01_frames[frame]);
  display.drawBitmap(0, 0, 128 / 8, 64, frameData);
  flushDisplay();
  
  // Servo keyframes: 5=left, 15=right, 25=left, 35=right, 45=center
  if (frame == 5) moveServoTo(SERVO_LEFT);
  else if (frame == 15) moveServoTo(SERVO_RIGHT);
  else if (frame == 25) moveServoTo(SERVO_LEFT);
  else if (frame == 35) moveServoTo(SERVO_RIGHT);
  else if (frame == 45) moveServoTo(SERVO_CENTER);
  
  frame++;
  if (frame >= LOVE01_FRAME_COUNT) {
    // Play once, return to idle
    Serial.println("💕 Love done - returning to idle");
    currentAnimation = "idle";
    currentTask = "";
    frame = 0;
    lastStart = 0;
    moveServoTo(SERVO_CENTER);
  }
}

void drawStartupAnimation() {
  static int frame = 0;
  static unsigned long lastFrameTime = 0;
  
  unsigned long now = millis();
  if (now - lastFrameTime < STARTUP01_FRAME_DELAY) return;
  lastFrameTime = now;
  
  // Draw frame
  display.clearBuffer();
  const uint8_t* frameData = (const uint8_t*)pgm_read_ptr(&startup01_frames[frame]);
  display.drawBitmap(0, 0, 128 / 8, 64, frameData);
  flushDisplay();
  
  // Servo keyframes: 15=left, 30=right, 45=center
  if (frame == 15) moveServoTo(SERVO_LEFT);
  else if (frame == 30) moveServoTo(SERVO_RIGHT);
  else if (frame == 45) moveServoTo(SERVO_CENTER);
  
  frame++;
  if (frame >= STARTUP01_FRAME_COUNT) {
    hasCompletedStartup = true;
    currentAnimation = "idle";
    frame = 0;
    moveServoTo(SERVO_CENTER);
  }
}

void drawAngryImage() {
  static int shakeStep = 0;
  static unsigned long lastShakeStepTime = 0;
  
  display.clearBuffer();
  display.drawBitmap(0, 0, 128 / 8, 64, angry_bitmap);
  flushDisplay();
  
  // Angry shake every 30 seconds
  unsigned long now = millis();
  if (now - lastPausedShakeTime >= 30000 && shakeStep == 0) {
    shakeStep = 1;
    lastPausedShakeTime = now;
    lastShakeStepTime = now;
    Serial.println("😠 Angry shake!");
  }
  
  // Execute shake sequence
  if (shakeStep > 0 && now - lastShakeStepTime >= 250) {
    lastShakeStepTime = now;
    switch (shakeStep) {
      case 1: moveServoTo(SERVO_LEFT); shakeStep = 2; break;
      case 2: moveServoTo(SERVO_RIGHT); shakeStep = 3; break;
      case 3: moveServoTo(SERVO_LEFT); shakeStep = 4; break;
      case 4: moveServoTo(SERVO_CENTER); shakeStep = 0; break;
    }
  }
}

void drawAngryAnimation() {
  static int frame = 0;
  static unsigned long lastAngryFrameTime = 0;
  static int shakeStep = 0;
  static unsigned long lastShakeStepTime = 0;

  unsigned long now = millis();

  // Advance + draw the animated angry face
  if (now - lastAngryFrameTime >= ANGRY01_FRAME_DELAY) {
    lastAngryFrameTime = now;
    display.clearBuffer();
    const uint8_t* frameData = (const uint8_t*)pgm_read_ptr(&angry01_frames[frame]);
    display.drawBitmap(0, 0, 128 / 8, 64, frameData);
    flushDisplay();
    if (++frame >= ANGRY01_FRAME_COUNT) frame = 0;
  }

  // Angry head-shake every 30 seconds (same behavior as the static image)
  if (now - lastPausedShakeTime >= 30000 && shakeStep == 0) {
    shakeStep = 1;
    lastPausedShakeTime = now;
    lastShakeStepTime = now;
    Serial.println("😠 Angry shake!");
  }
  if (shakeStep > 0 && now - lastShakeStepTime >= 250) {
    lastShakeStepTime = now;
    switch (shakeStep) {
      case 1: moveServoTo(SERVO_LEFT); shakeStep = 2; break;
      case 2: moveServoTo(SERVO_RIGHT); shakeStep = 3; break;
      case 3: moveServoTo(SERVO_LEFT); shakeStep = 4; break;
      case 4: moveServoTo(SERVO_CENTER); shakeStep = 0; break;
    }
  }
}

void drawPomodoroAnimation() {
  static int frame = 0;
  frame++;
  
  display.clearBuffer();
  
  // Focused face
  display.setFont(u8g2_font_10x20_tf);
  display.drawStr(45, 20, "(>.<)");
  
  // Task name
  display.setFont(u8g2_font_6x10_tf);
  String taskDisplay = currentTask;
  if (taskDisplay.length() > 21) {
    taskDisplay = taskDisplay.substring(0, 18) + "...";
  }
  display.drawStr(0, 35, taskDisplay.c_str());
  
  // Focus indicator
  display.drawStr(30, 48, "FOCUS!");
  
  if ((frame / 10) % 2 == 0) {
    display.drawStr(85, 48, "[*]");
  } else {
    display.drawStr(85, 48, "[!]");
  }
  
  // Progress bar
  int progress = (frame * 2) % 128;
  display.drawFrame(0, 55, 128, 8);
  display.drawBox(1, 56, progress, 6);
  
  flushDisplay();
}

void drawTaskCompleteAnimation() {
  static int frame = 0;
  static int servoStep = 0;
  static unsigned long lastServoTime = 0;
  static unsigned long lastStart = 0;
  
  // Reset on start
  if (animationStartTime != lastStart) {
    frame = 0;
    servoStep = 0;
    lastServoTime = 0;
    lastStart = animationStartTime;
  }
  
  frame++;
  
  display.clearBuffer();
  display.setFont(u8g2_font_10x20_tf);
  display.drawStr(45, 20, "(^.^)");
  display.setFont(u8g2_font_6x10_tf);
  display.drawStr(20, 35, "Great job!");
  
  String taskDisplay = currentTask;
  if (taskDisplay.length() > 21) taskDisplay = taskDisplay.substring(0, 18) + "...";
  display.drawStr(0, 50, taskDisplay.c_str());
  
  if (frame % 20 < 10) {
    display.drawPixel(20, 15);
    display.drawPixel(100, 20);
  }
  flushDisplay();
  
  // Simple wiggle: left-right-left-right-center
  unsigned long now = millis();
  if (now - lastServoTime >= 200 && servoStep < 5) {
    lastServoTime = now;
    switch (servoStep) {
      case 0: moveServoTo(SERVO_LEFT); break;
      case 1: moveServoTo(SERVO_RIGHT); break;
      case 2: moveServoTo(SERVO_LEFT); break;
      case 3: moveServoTo(SERVO_RIGHT); break;
      case 4: moveServoTo(SERVO_CENTER); break;
    }
    servoStep++;
  }
  
  // Return to idle after 5 seconds
  if (millis() - animationStartTime > 5000) {
    currentAnimation = "idle";
    currentTask = "";
    moveServoTo(SERVO_CENTER);
  }
}

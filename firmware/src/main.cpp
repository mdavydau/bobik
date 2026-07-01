#include <Wire.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <U8g2lib.h>
#include <Preferences.h>
#include <ESPmDNS.h>
#include <DNSServer.h>
#include <ESP32Servo.h>

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
void drawPomodoroAnimation();
void drawTaskCompleteAnimation();
void drawDebugInfo();
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
  display.sendBuffer();
  
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
  html += "<style>body{font-family:Arial,sans-serif;max-width:600px;margin:50px auto;padding:20px;}";
  html += ".status{background:#e8f5e8;padding:15px;border-radius:8px;margin:20px 0;}";
  html += ".button{background:#007bff;color:white;padding:10px 20px;border:none;border-radius:5px;margin:5px;cursor:pointer;}";
  html += ".button:hover{background:#0056b3;}</style></head><body>";
  html += "<h1>Tabbie Assistant</h1>";
  html += "<div class='status'><h3>Status: Connected!</h3>";
  html += "<p>Current Animation: <span id='current-animation'>Loading...</span></p>";
  html += "<p>WiFi: " + WiFi.SSID() + "</p>";
  html += "<p>IP: " + WiFi.localIP().toString() + "</p></div>";
  html += "<h3>Test Animations:</h3>";
  html += "<button class='button' onclick=\"sendAnimation('idle')\">Idle</button>";
  html += "<button class='button' onclick=\"sendAnimation('pomodoro','Focus Session')\">Pomodoro</button>";
  html += "<button class='button' onclick=\"sendAnimation('complete','Task Done!')\">Complete</button>";
  html += "<h3>Settings:</h3>";
  html += "<button class='button' onclick=\"window.location='/wifi'\">WiFi Settings</button>";
  html += "<button class='button' style='background:#dc3545;' onclick=\"resetWiFi()\">Reset WiFi</button>";
  html += "<script>";
  html += "async function resetWiFi(){if(confirm('This will clear WiFi settings and restart Tabbie. Continue?')){try{await fetch('/api/reset',{method:'POST'});alert('Tabbie is restarting in setup mode...');}catch(e){alert('Tabbie is restarting...');}}}";
  html += "async function sendAnimation(type,task=''){";
  html += "try{const response=await fetch('/api/animation',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({animation:type,task:task})});";
  html += "if(response.ok){updateStatus();}}catch(e){console.error('Failed to send animation:',e);}}";
  html += "async function updateStatus(){try{const response=await fetch('/api/status');const data=await response.json();";
  html += "document.getElementById('current-animation').textContent=data.animation;}catch(e){console.error('Failed to get status:',e);}}";
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

#ifdef TABBIE_MQTT
// Apply an animation command from the MQTT bridge. Mirrors handleAnimation().
void applyAnimation(const String& anim, const String& task, unsigned long durSec) {
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

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  // Accept JSON {"animation":"..","task":"..","duration":N} or a bare name string.
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, payload, length);
  if (!err) {
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
  } else if (currentAnimation == "pomodoro") {
    drawPomodoroAnimation();
  } else if (currentAnimation == "complete") {
    drawTaskCompleteAnimation();
  }
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
    // Connected - show IP prominently
    display.drawStr(0, 10, "=== CONNECTED ===");
    
    // IP Address - the most important info!
    display.setFont(u8g2_font_7x13B_tf); // Slightly bigger font for IP
    display.drawStr(0, 26, WiFi.localIP().toString().c_str());
    display.setFont(u8g2_font_6x10_tf);
    
    // WiFi name
    String ssidDisplay = WiFi.SSID();
    if (ssidDisplay.length() > 18) {
      ssidDisplay = ssidDisplay.substring(0, 15) + "...";
    }
    display.drawStr(0, 40, ssidDisplay.c_str());
    
    // Signal strength with visual indicator
    int rssi = WiFi.RSSI();
    String signal;
    if (rssi > -50) signal = "Signal: Great";
    else if (rssi > -60) signal = "Signal: Good";
    else if (rssi > -70) signal = "Signal: Fair";
    else signal = "Signal: Weak";
    display.drawStr(0, 52, signal.c_str());
    
    // Countdown
    int secondsLeft = (DEBUG_MODE_DURATION - (millis() - debugModeStartTime)) / 1000;
    String countdown = "(" + String(secondsLeft) + "s)";
    display.drawStr(100, 52, countdown.c_str());
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
  
  display.sendBuffer();
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
  
  display.sendBuffer();
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
  
  display.sendBuffer();
}

void drawConnected() {
  display.clearBuffer();
  display.setFont(u8g2_font_6x10_tf);
  
  display.drawStr(0, 10, "Connected!");
  display.drawStr(0, 24, WiFi.SSID().c_str());
  display.drawStr(0, 38, WiFi.localIP().toString().c_str());
  
  display.sendBuffer();
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
  
  display.sendBuffer();
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
  display.sendBuffer();
  
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
  
  display.sendBuffer();
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
  display.sendBuffer();
  
  // Servo keyframes: 40=left, 50=right, 60=center (every loop)
  if (frame == 40) moveServoTo(SERVO_LEFT);
  else if (frame == 50) moveServoTo(SERVO_RIGHT);
  else if (frame == 60) moveServoTo(SERVO_CENTER);
  
  frame++;
  if (frame >= RELAX01_FRAME_COUNT) frame = 0;
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
  display.sendBuffer();
  
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
  display.sendBuffer();
  
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
  display.sendBuffer();
  
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
    display.sendBuffer();
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
  
  display.sendBuffer();
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
  display.sendBuffer();
  
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
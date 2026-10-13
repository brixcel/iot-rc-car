/*
 * =====================================================================================
 *                CYBER-RC // NODEMCU ESP8266 MQTT CLOUD IOT BRIDGE
 *                True Internet of Things (IoT) Remote Telemetry & Control
 * =====================================================================================
 * Target Board: NodeMCU 1.0 (ESP-12E Module) / ESP8266
 * Function:     1. Connects to Follower's Mobile Hotspot (4G/5G Cellular Internet)
 *               2. Connects to a Cloud MQTT Broker (TLS) via Internet
 *               3. Subscribes to commands published from 2nd Floor (or anywhere globally)
 *               4. Bridges commands to Arduino Uno via SoftwareSerial (Pins D1 & D2)
 *               5. Built-in Watchdog Failsafe (auto-stops car if internet signal drops)
 * 
 * --- REQUIRED ARDUINO LIBRARY ---
 * Install via Arduino IDE:
 *   Sketch -> Include Library -> Manage Libraries -> Search "PubSubClient" -> Install
 * 
 * --- PIN WIRING TO ARDUINO UNO (NO CHANGES NEEDED) ---
 *   NodeMCU Pin D1 (GPIO 5 - TX) ---> Arduino Uno Pin 2 (RX)
 *   NodeMCU Pin D2 (GPIO 4 - RX) <--- Arduino Uno Pin 3 (TX) [Telemetry/Feedback]
 *   NodeMCU GND                  <--- Arduino Uno GND (MANDATORY COMMON GROUND)
 *   NodeMCU VIN                  <--- 5V Power (from Arduino 5V / L298N 5V out)
 * =====================================================================================
 */

#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <SoftwareSerial.h>

// =====================================================================================
//                             CONFIGURATION SETTINGS
// =====================================================================================

// 1 & 2. HOTSPOT + MQTT BROKER CREDENTIALS
// Kept in secrets.h (git-ignored) so passwords never get pushed to GitHub.
// First time: copy secrets.example.h to secrets.h and fill it in.
#include "secrets.h"

// 3. UNIQUE TOPIC IDENTIFIER
// Must match the Topic in your Web Cockpit settings (default: "cyber_rc_car")
const char* TOPIC_CMD    = "cyber_rc_car/cmd";     // Receives driving commands
const char* TOPIC_STATUS = "cyber_rc_car/status";  // Sends telemetry to cockpit
const char* TOPIC_PONG   = "cyber_rc_car/pong";    // Echoes latency pings back to cockpit

// =====================================================================================

// SoftwareSerial communication to Arduino Uno (RX=D2, TX=D1)
SoftwareSerial unoSerial(D2, D1); // D2 = RX (GPIO 4), D1 = TX (GPIO 5)
const long UNO_BAUD = 38400; // Must match LINK_BAUD in Arduino_RC_Car.ino

// Wi-Fi and MQTT Client instances (TLS: EMQX Serverless only accepts port 8883)
WiFiClientSecure espClient;
PubSubClient mqttClient(espClient);

// Watchdog & Safety variables
unsigned long lastCommandTime = 0;
bool carIsActive = false;
const unsigned long AUTO_STOP_TIMEOUT = 1200; // ms: Stops car if no command within 1.2s

// Active drive command, re-sent to the Uno every 100ms so a dropped serial byte
// is corrected on the next refresh and the Uno's 500ms failsafe never fires mid-drive
char driveCmd[32] = "S:0";
unsigned long lastRefreshTime = 0;
const unsigned long REFRESH_INTERVAL = 100;

// Non-blocking reconnect timers
unsigned long lastReconnectAttempt = 0;
unsigned long lastTelemetryTime = 0;
unsigned long lastWiFiCheckTime = 0;

// LED Indicator (NodeMCU onboard LED is active LOW: LOW = ON, HIGH = OFF)
const int PIN_LED = LED_BUILTIN; // GPIO 2 (D4)

// --- FORWARD DECLARATIONS ---
void setupWiFi();
void mqttCallback(char* topic, byte* payload, unsigned int length);
boolean reconnectMQTT();

void setup() {
  // Initialize Hardware USB Serial for PC debugging
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println(F("================================================"));
  Serial.println(F("    CYBER-RC // NODEMCU MQTT CLOUD BRIDGE       "));
  Serial.println(F("================================================"));

  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, HIGH); // Turn LED off initially

  // Initialize SoftwareSerial to Arduino Uno
  unoSerial.begin(UNO_BAUD);
  Serial.println(F("[SERIAL]: SoftwareSerial active on D1(TX)->Uno(2) and D2(RX)->Uno(3) at 38400 baud"));

  // Connect to Follower's Mobile Hotspot with robust reconnection settings
  setupWiFi();

  // Configure MQTT Cloud Client
  mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
  mqttClient.setCallback(mqttCallback);
  mqttClient.setBufferSize(256); // Ensure sufficient buffer for packets
  espClient.setNoDelay(true);    // Disable Nagle: send small packets (pongs) immediately
  // Encrypts traffic but skips verifying the broker's certificate (no clock/CA bundle
  // needed on the ESP8266). Fine for a school project; not for anything sensitive.
  espClient.setInsecure();

  // Send initial stop command to Arduino
  unoSerial.println("S:0");
  Serial.println(F("[READY]: Listening for Cloud MQTT Commands...\n"));
}

void loop() {
  unsigned long now = millis();

  // 0. Watchdog Failsafe: Auto-stop car if the command stream stopped while moving.
  //    Runs before the Wi-Fi check so it still fires when Wi-Fi drops.
  if (carIsActive && (now - lastCommandTime > AUTO_STOP_TIMEOUT)) {
    Serial.println(F("[WATCHDOG]: Command stream timeout! Stopping RC Car."));
    strcpy(driveCmd, "S:0");
    unoSerial.println(driveCmd);
    carIsActive = false;
  }

  // 0b. Refresh the active drive command to the Uno
  if (now - lastRefreshTime >= REFRESH_INTERVAL) {
    lastRefreshTime = now;
    unoSerial.println(driveCmd);
  }

  // 1. Maintain Wi-Fi Connection (Smooth background auto-reconnect)
  if (WiFi.status() != WL_CONNECTED) {
    // Fast blink LED to visibly show it is searching for hotspot
    digitalWrite(PIN_LED, (now % 400 < 200) ? LOW : HIGH);

    if (now - lastWiFiCheckTime > 8000) {
      lastWiFiCheckTime = now;
      Serial.println(F("[WIFI]: Searching / reconnecting to hotspot..."));
      WiFi.reconnect();
    }
    return; // Don't try MQTT while Wi-Fi is down
  }

  // 2. Maintain MQTT Cloud Connection (Non-blocking reconnect)
  if (!mqttClient.connected()) {
    // Slow blink LED to show Wi-Fi is connected but waiting for MQTT Cloud
    digitalWrite(PIN_LED, (now % 1000 < 500) ? LOW : HIGH);

    if (now - lastReconnectAttempt > 4000) {
      lastReconnectAttempt = now;
      if (reconnectMQTT()) {
        lastReconnectAttempt = 0;
      }
    }
  } else {
    // Process incoming MQTT messages
    mqttClient.loop();
    digitalWrite(PIN_LED, LOW); // Solid blue LED = 100% HEALTHY CLOUD LINK
  }

  // 3. Send Periodic Telemetry/Heartbeat to Cloud (Every 3 seconds)
  if (now - lastTelemetryTime > 3000) {
    lastTelemetryTime = now;
    if (mqttClient.connected()) {
      String json = "{\"status\":\"ONLINE\",\"rssi\":" + String(WiFi.RSSI()) + ",\"uptime\":" + String(now / 1000) + "}";
      mqttClient.publish(TOPIC_STATUS, json.c_str());
    }
  }

  // 4. Forward any telemetry/log messages from Arduino Uno to PC Serial Monitor
  while (unoSerial.available() > 0) {
    char c = (char)unoSerial.read();
    Serial.write(c);
  }
}

// --- WI-FI CONNECTION HANDLER ---
void setupWiFi() {
  Serial.println();
  Serial.print(F("[WIFI]: Connecting to Hotspot: "));
  Serial.println(WIFI_SSID);

  WiFi.persistent(false);          // Prevent flash wear and stale cached configs
  WiFi.disconnect(true);           // Clear old state
  delay(100);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);     // Let ESP8266 radio auto-reconnect in hardware
  WiFi.setSleepMode(WIFI_NONE_SLEEP); // Disable power save sleep (keeps Wi-Fi always awake)
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(300);
    Serial.print(F("."));
    digitalWrite(PIN_LED, (attempts % 2 == 0) ? LOW : HIGH); // Blink while connecting
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println();
    Serial.print(F("[WIFI]: Connected! IP assigned by Hotspot: "));
    Serial.println(WiFi.localIP());
    Serial.print(F("[WIFI]: Signal Strength (RSSI): "));
    Serial.print(WiFi.RSSI());
    Serial.println(F(" dBm"));
  } else {
    Serial.println();
    Serial.println(F("[WIFI]: Initial connection timeout. Background auto-reconnect armed."));
  }
}

// --- MQTT CLOUD RECONNECT HANDLER ---
boolean reconnectMQTT() {
  Serial.print(F("[MQTT]: Connecting to Cloud Broker ("));
  Serial.print(MQTT_BROKER);
  Serial.print(F(")... "));

  // Generate a unique Client ID based on NodeMCU Chip ID
  String clientId = "CYBER_RC_NODE_" + String(ESP.getChipId(), HEX);

  if (mqttClient.connect(clientId.c_str(), MQTT_USER, MQTT_PASS)) {
    Serial.println(F("CONNECTED!"));
    
    // Subscribe to the command topic
    mqttClient.subscribe(TOPIC_CMD);
    Serial.print(F("[MQTT]: Subscribed to topic: "));
    Serial.println(TOPIC_CMD);

    // Announce online status
    mqttClient.publish(TOPIC_STATUS, "{\"status\":\"ONLINE\"}");
    return true;
  } else {
    Serial.print(F("FAILED. State code: "));
    Serial.println(mqttClient.state());
    return false;
  }
}

// --- MQTT MESSAGE RECEIVED CALLBACK ---
// This function runs automatically whenever a command arrives from the Web Cockpit!
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  // Convert payload byte buffer into a clean null-terminated string
  char msg[64];
  if (length >= sizeof(msg)) length = sizeof(msg) - 1;
  memcpy(msg, payload, length);
  msg[length] = '\0';

  // Latency ping from the cockpit: echo it straight back, don't forward to the Uno
  if (strncmp(msg, "PING:", 5) == 0) {
    mqttClient.publish(TOPIC_PONG, msg + 5);
    return;
  }

  // Send packet directly to Arduino Uno via SoftwareSerial!
  // e.g., "F:200\n", "S:0\n", "V:100:0:200\n", "LIGHT:1\n", "HORN:1\n"
  unoSerial.println(msg);

  // Lights/horn are one-shot: don't let them replace the active drive command
  if (strncmp(msg, "LIGHT", 5) == 0 || strncmp(msg, "HORN", 4) == 0) {
    return;
  }

  // Debug log on USB Serial Monitor (only when the drive command changes)
  if (strcmp(msg, driveCmd) != 0) {
    Serial.print(F("[MQTT -> UNO]: "));
    Serial.println(msg);
  }

  strncpy(driveCmd, msg, sizeof(driveCmd) - 1);
  driveCmd[sizeof(driveCmd) - 1] = '\0';
  lastCommandTime = millis();
  // Stop is exactly "S" / "S:<n>"; "SL"/"SR" are spins and count as moving
  carIsActive = !(msg[0] == 'S' && (msg[1] == ':' || msg[1] == '\0'));
}

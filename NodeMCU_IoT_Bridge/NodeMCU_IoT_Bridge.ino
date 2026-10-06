/*
 * =====================================================================================
 *                CYBER-RC // NODEMCU ESP8266 MQTT CLOUD IOT BRIDGE
 *                True Internet of Things (IoT) Remote Telemetry & Control
 * =====================================================================================
 * Target Board: NodeMCU 1.0 (ESP-12E Module) / ESP8266
 * Function:     1. Connects to Follower's Mobile Hotspot (4G/5G Cellular Internet)
 *               2. Connects to HiveMQ Cloud MQTT Broker via Internet
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
#include <PubSubClient.h>
#include <SoftwareSerial.h>

// =====================================================================================
//                             CONFIGURATION SETTINGS
// =====================================================================================

// 1. FOLLOWER'S MOBILE HOTSPOT CREDENTIALS
// Enter the exact Hotspot name and password of the phone following the RC car
const char* WIFI_SSID = "Redmi 9T";
const char* WIFI_PASS = "brexcel14";

// 2. MQTT CLOUD BROKER SETTINGS
// Free public HiveMQ broker (No account or API key required!)
const char* MQTT_BROKER = "broker.hivemq.com";
const int   MQTT_PORT   = 1883; // Standard unencrypted MQTT TCP port

// 3. UNIQUE TOPIC IDENTIFIER
// Must match the Topic in your Web Cockpit settings (default: "cyber_rc_car")
const char* TOPIC_CMD    = "cyber_rc_car/cmd";     // Receives driving commands
const char* TOPIC_STATUS = "cyber_rc_car/status";  // Sends telemetry to cockpit

// =====================================================================================

// SoftwareSerial communication to Arduino Uno (RX=D2, TX=D1)
SoftwareSerial unoSerial(D2, D1); // D2 = RX (GPIO 4), D1 = TX (GPIO 5)

// Wi-Fi and MQTT Client instances
WiFiClient espClient;
PubSubClient mqttClient(espClient);

// Watchdog & Safety variables
unsigned long lastCommandTime = 0;
bool carIsActive = false;
const unsigned long AUTO_STOP_TIMEOUT = 1500; // ms: Stops car if no command within 1.5s

// Non-blocking reconnect timers
unsigned long lastReconnectAttempt = 0;
unsigned long lastTelemetryTime = 0;

// LED Indicator (NodeMCU onboard LED is active LOW)
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

  // Initialize SoftwareSerial to Arduino Uno at 9600 baud
  unoSerial.begin(9600);
  Serial.println(F("[SERIAL]: SoftwareSerial active on D1(TX)->Uno(2) and D2(RX)->Uno(3) at 9600 baud"));

  // Connect to Follower's Mobile Hotspot
  setupWiFi();

  // Configure MQTT Cloud Client
  mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
  mqttClient.setCallback(mqttCallback);
  mqttClient.setBufferSize(256); // Ensure sufficient buffer for packets

  // Send initial stop command to Arduino
  unoSerial.println("S:0");
  Serial.println(F("[READY]: Listening for Cloud MQTT Commands...\n"));
}

void loop() {
  // 1. Maintain Wi-Fi Connection
  if (WiFi.status() != WL_CONNECTED) {
    digitalWrite(PIN_LED, HIGH);
    setupWiFi();
  }

  // 2. Maintain MQTT Cloud Connection (Non-blocking reconnect)
  if (!mqttClient.connected()) {
    digitalWrite(PIN_LED, HIGH);
    unsigned long now = millis();
    if (now - lastReconnectAttempt > 5000) {
      lastReconnectAttempt = now;
      if (reconnectMQTT()) {
        lastReconnectAttempt = 0;
      }
    }
  } else {
    // Process incoming MQTT messages
    mqttClient.loop();
    digitalWrite(PIN_LED, LOW); // LED ON indicates healthy Cloud connection
  }

  // 3. Watchdog Failsafe: Auto-stop car if signal was lost while moving
  if (carIsActive && (millis() - lastCommandTime > AUTO_STOP_TIMEOUT)) {
    Serial.println(F("[WATCHDOG]: Command stream timeout! Stopping RC Car."));
    unoSerial.println("S:0");
    carIsActive = false;
  }

  // 4. Send Periodic Telemetry/Heartbeat to Cloud (Every 3 seconds)
  if (millis() - lastTelemetryTime > 3000) {
    lastTelemetryTime = millis();
    if (mqttClient.connected()) {
      String json = "{\"status\":\"ONLINE\",\"rssi\":" + String(WiFi.RSSI()) + ",\"uptime\":" + String(millis() / 1000) + "}";
      mqttClient.publish(TOPIC_STATUS, json.c_str());
    }
  }

  // 5. Forward any telemetry/log messages from Arduino Uno to PC Serial Monitor
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

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 25) {
    delay(400);
    Serial.print(F("."));
    digitalWrite(PIN_LED, !digitalRead(PIN_LED)); // Blink while connecting
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
    Serial.println(F("[WIFI]: Connection pending... will retry in loop."));
  }
}

// --- MQTT CLOUD RECONNECT HANDLER ---
boolean reconnectMQTT() {
  Serial.print(F("[MQTT]: Connecting to HiveMQ Cloud Broker ("));
  Serial.print(MQTT_BROKER);
  Serial.print(F(")... "));

  // Generate a unique Client ID based on NodeMCU Chip ID
  String clientId = "CYBER_RC_NODE_" + String(ESP.getChipId(), HEX);

  if (mqttClient.connect(clientId.c_str())) {
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

  // Debug log on USB Serial Monitor
  Serial.print(F("[MQTT -> UNO]: "));
  Serial.println(msg);

  // Send packet directly to Arduino Uno via SoftwareSerial!
  // e.g., "F:200\n", "S:0\n", "V:100:0:200\n", "LIGHT:1\n", "HORN:1\n"
  unoSerial.println(msg);

  lastCommandTime = millis();
  // If direction is not Stop ('S'), mark car as actively moving
  carIsActive = (msg[0] != 'S');
}

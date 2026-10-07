/*
 * =====================================================================================
 *                       CYBER-RC // ARDUINO UNO FIRMWARE
 *                Dual-Microcontroller IoT RC Car Architecture
 * =====================================================================================
 * Target Board: Arduino Uno (or Nano / Mega)
 * Motor Driver: L298N Dual H-Bridge Motor Driver
 * IoT Bridge:   NodeMCU ESP8266 (Receives Wi-Fi commands, forwards via Serial)
 * 
 * --- PIN WIRING CONFIGURATION ---
 * [SoftwareSerial to NodeMCU]
 *   Arduino Pin 2 (RX)  <---  NodeMCU Pin D1 (TX - GPIO 5)
 *   Arduino Pin 3 (TX)  --->  NodeMCU Pin D2 (RX - GPIO 4) [Optional / Telemetry]
 *   Arduino GND         <---  NodeMCU GND (MANDATORY COMMON GROUND)
 * 
 * [L298N Motor Driver Connections]
 *   Arduino Pin 5 (PWM) --->  L298N ENA (Left Motor Speed)
 *   Arduino Pin 6       --->  L298N IN1 (Left Motor Forward)
 *   Arduino Pin 7       --->  L298N IN2 (Left Motor Reverse)
 *   Arduino Pin 8       --->  L298N IN3 (Right Motor Forward)
 *   Arduino Pin 9       --->  L298N IN4 (Right Motor Reverse)
 *   Arduino Pin 10(PWM) --->  L298N ENB (Right Motor Speed)
 *   Arduino GND         <---  L298N GND (MANDATORY COMMON GROUND)
 * 
 * [Auxiliary Accessories]
 *   Arduino Pin 11      --->  Buzzer / Horn (+) (Optional)
 *   Arduino Pin 12      --->  Front LED Headlights (+) (Optional)
 * 
 * Note: Hardware Serial (Pins 0 & 1) is kept FREE for USB debugging at 9600 baud!
 * =====================================================================================
 */

#include <SoftwareSerial.h>

// SoftwareSerial for NodeMCU communication (Pin 2 = RX, Pin 3 = TX)
SoftwareSerial nodeSerial(2, 3);

// --- MOTOR DRIVER PIN DEFINITIONS (L298N) ---
// User's exact confirmed wiring:
const int PIN_IN1 = 10;  // Left Motor 1
const int PIN_IN2 = 11;  // Left Motor 2
const int PIN_IN3 = 12;  // Right Motor 1
const int PIN_IN4 = 13;  // Right Motor 2
const int PIN_ENA = -1;  // -1 for jumpered ENA
const int PIN_ENB = -1;  // -1 for jumpered ENB

// --- AUXILIARY PINS (Using Analog pins to leave D10..D13 free) ---
const int PIN_BUZZER = A0; // Horn / Buzzer (Optional)
const int PIN_LIGHTS = A1; // Headlights LED (Optional)

// --- STATE VARIABLES ---
int currentSpeed = 200;            // Default speed (0 - 255)
unsigned long lastCommandTime = 0; // For failsafe auto-stop
const unsigned long FAILSAFE_TIMEOUT_MS = 1800; // Auto-stop after 1.8s of no signal
bool isMoving = false;

// Serial reception buffer
String rxBuffer = "";

void setup() {
  // Initialize Hardware USB Serial for PC debugging
  Serial.begin(9600);
  Serial.println(F("========================================"));
  Serial.println(F("   CYBER-RC // ARDUINO UNO READY        "));
  Serial.println(F("   Listening to NodeMCU on Pins 2 & 3   "));
  Serial.println(F("========================================"));

  // Initialize SoftwareSerial for NodeMCU link
  nodeSerial.begin(9600);

  // Configure Motor Pins
  if (PIN_ENA != -1) pinMode(PIN_ENA, OUTPUT);
  pinMode(PIN_IN1, OUTPUT);
  pinMode(PIN_IN2, OUTPUT);
  pinMode(PIN_IN3, OUTPUT);
  pinMode(PIN_IN4, OUTPUT);
  if (PIN_ENB != -1) pinMode(PIN_ENB, OUTPUT);

  // Configure Auxiliary Pins
  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_LIGHTS, OUTPUT);
  digitalWrite(PIN_BUZZER, LOW);
  digitalWrite(PIN_LIGHTS, LOW);

  // Start with motors stopped
  stopMotors();

  // Startup beep confirmation
  tone(PIN_BUZZER, 1000, 100);
  delay(150);
  tone(PIN_BUZZER, 1500, 150);

  // Self-Test: 400ms motor pulse to visually verify battery & L298N are working
  Serial.println(F("[HARDWARE CHECK]: Pulsing motors forward for 400ms..."));
  digitalWrite(PIN_IN1, HIGH);
  digitalWrite(PIN_IN2, LOW);
  digitalWrite(PIN_IN3, HIGH);
  digitalWrite(PIN_IN4, LOW);
  delay(400);
  stopMotors();
  Serial.println(F("[HARDWARE CHECK]: Pulse complete. Ready for NodeMCU commands."));
}

void loop() {
  // 1. Read incoming commands from NodeMCU via SoftwareSerial
  while (nodeSerial.available() > 0) {
    char c = (char)nodeSerial.read();
    // Debug echo: show exact bytes arriving from NodeMCU
    Serial.print(F("[LINK RX]: '"));
    Serial.print(c);
    Serial.print(F("' (0x"));
    Serial.print((byte)c, HEX);
    Serial.println(F(")"));

    if (c == '\n' || c == '\r') {
      if (rxBuffer.length() > 0) {
        processCommand(rxBuffer);
        rxBuffer = "";
      }
    } else {
      rxBuffer += c;
    }
  }

  // 2. Also allow USB Serial Monitor commands for easy manual testing on PC!
  while (Serial.available() > 0) {
    char c = (char)Serial.read();
    if (c == '\n' || c == '\r') {
      if (rxBuffer.length() > 0) {
        Serial.print(F("[USB TEST]: "));
        Serial.println(rxBuffer);
        processCommand(rxBuffer);
        rxBuffer = "";
      }
    } else {
      rxBuffer += c;
    }
  }

  // 3. Failsafe Watchdog: Auto-stop if signal lost
  if (isMoving && (millis() - lastCommandTime > FAILSAFE_TIMEOUT_MS)) {
    Serial.println(F("[FAILSAFE]: Signal timed out! Emergency stop triggered."));
    stopMotors();
  }
}

/**
 * Parses and executes incoming commands
 * Command Formats:
 *   "F:200"        -> Forward at speed 200
 *   "B:200"        -> Backward at speed 200
 *   "L:200"        -> Spin Left at speed 200
 *   "R:200"        -> Spin Right at speed 200
 *   "G:200"        -> Forward Left
 *   "I:200"        -> Forward Right
 *   "H:200"        -> Reverse Left
 *   "J:200"        -> Reverse Right
 *   "S:0"          -> Immediate Stop
 *   "V:<x>:<y>:<s">-> Analog Vector Joystick Steering
 *   "LIGHT:<1/0>"  -> Toggle Headlights
 *   "HORN:<1/0>"   -> Sound Horn
 */
void processCommand(String cmdStr) {
  cmdStr.trim();
  if (cmdStr.length() == 0) return;

  lastCommandTime = millis();

  // Split command by colon ':'
  int firstColon = cmdStr.indexOf(':');
  String action = (firstColon == -1) ? cmdStr : cmdStr.substring(0, firstColon);
  action.toUpperCase();

  int spd = currentSpeed;
  if (firstColon != -1) {
    spd = cmdStr.substring(firstColon + 1).toInt();
    if (spd <= 0 && action != "S" && action != "LIGHT" && action != "HORN") {
      spd = currentSpeed;
    }
    spd = constrain(spd, 0, 255);
  }

  // Debug output to PC Serial
  Serial.print(F("CMD: "));
  Serial.print(action);
  Serial.print(F(" | SPD: "));
  Serial.println(spd);

  if (action == "F") {
    moveForward(spd);
  } else if (action == "B") {
    moveBackward(spd);
  } else if (action == "L") {
    // Smooth Forward-Left Turn: Right wheel drives, Left wheel pivots
    setMotors(0, spd);
  } else if (action == "R") {
    // Smooth Forward-Right Turn: Left wheel drives, Right wheel pivots
    setMotors(spd, 0);
  } else if (action == "G") {
    // Forward-Left curve
    setMotors(0, spd);
  } else if (action == "I") {
    // Forward-Right curve
    setMotors(spd, 0);
  } else if (action == "H") {
    // Reverse-Left curve
    setMotors(0, -spd);
  } else if (action == "J") {
    // Reverse-Right curve
    setMotors(-spd, 0);
  } else if (action == "SL") {
    // 360° Spin Left in place
    spinLeft(spd);
  } else if (action == "SR") {
    // 360° Spin Right in place
    spinRight(spd);
  } else if (action == "S") {
    stopMotors();
  } else if (action == "V") {
    // Format: V:<normX>:<normY>:<spd>
    // e.g., V:50:80:200
    int secondColon = cmdStr.indexOf(':', firstColon + 1);
    if (secondColon != -1) {
      int normX = cmdStr.substring(firstColon + 1, secondColon).toInt();
      int thirdColon = cmdStr.indexOf(':', secondColon + 1);
      int normY = 0;
      int baseSpd = currentSpeed;
      if (thirdColon != -1) {
        normY = cmdStr.substring(secondColon + 1, thirdColon).toInt();
        baseSpd = cmdStr.substring(thirdColon + 1).toInt();
      } else {
        normY = cmdStr.substring(secondColon + 1).toInt();
      }
      processVector(normX, normY, baseSpd);
    }
  } else if (action == "LIGHT") {
    int val = (firstColon != -1) ? cmdStr.substring(firstColon + 1).toInt() : 0;
    digitalWrite(PIN_LIGHTS, val > 0 ? HIGH : LOW);
  } else if (action == "HORN") {
    int val = (firstColon != -1) ? cmdStr.substring(firstColon + 1).toInt() : 0;
    if (val > 0) {
      tone(PIN_BUZZER, 450);
    } else {
      noTone(PIN_BUZZER);
    }
  }
}

/**
 * Smooth Analog Vector Differential Steering
 * normX: -100 (full left) to +100 (full right)
 * normY: -100 (full reverse) to +100 (full forward)
 */
void processVector(int normX, int normY, int baseSpeed) {
  if (abs(normX) < 15 && abs(normY) < 15) {
    stopMotors();
    return;
  }

  // Differential mix:
  // Left Motor = Y + X * 0.8
  // Right Motor = Y - X * 0.8
  float fY = (float)normY / 100.0;
  float fX = (float)normX / 100.0;

  float leftRatio = fY + (fX * 0.75);
  float rightRatio = fY - (fX * 0.75);

  int leftSpd = constrain((int)(leftRatio * baseSpeed), -255, 255);
  int rightSpd = constrain((int)(rightRatio * baseSpeed), -255, 255);

  setMotors(leftSpd, rightSpd);
}

// Low-level motor control
// Positive speed = Forward, Negative speed = Reverse, 0 = Stop
void setMotors(int leftSpd, int rightSpd) {
  // LEFT MOTOR CONTROL
  if (leftSpd > 10) {
    digitalWrite(PIN_IN1, HIGH);
    digitalWrite(PIN_IN2, LOW);
    if (PIN_ENA != -1) analogWrite(PIN_ENA, constrain(leftSpd, 0, 255));
  } else if (leftSpd < -10) {
    digitalWrite(PIN_IN1, LOW);
    digitalWrite(PIN_IN2, HIGH);
    if (PIN_ENA != -1) analogWrite(PIN_ENA, constrain(-leftSpd, 0, 255));
  } else {
    digitalWrite(PIN_IN1, LOW);
    digitalWrite(PIN_IN2, LOW);
    if (PIN_ENA != -1) analogWrite(PIN_ENA, 0);
  }

  // RIGHT MOTOR CONTROL
  if (rightSpd > 10) {
    digitalWrite(PIN_IN3, HIGH);
    digitalWrite(PIN_IN4, LOW);
    if (PIN_ENB != -1) analogWrite(PIN_ENB, constrain(rightSpd, 0, 255));
  } else if (rightSpd < -10) {
    digitalWrite(PIN_IN3, LOW);
    digitalWrite(PIN_IN4, HIGH);
    if (PIN_ENB != -1) analogWrite(PIN_ENB, constrain(-rightSpd, 0, 255));
  } else {
    digitalWrite(PIN_IN3, LOW);
    digitalWrite(PIN_IN4, LOW);
    if (PIN_ENB != -1) analogWrite(PIN_ENB, 0);
  }

  isMoving = (abs(leftSpd) > 10 || abs(rightSpd) > 10);
}

void moveForward(int spd) {
  setMotors(spd, spd);
}

void moveBackward(int spd) {
  setMotors(-spd, -spd);
}

void spinLeft(int spd) {
  setMotors(-spd, spd);
}

void spinRight(int spd) {
  setMotors(spd, -spd);
}

void stopMotors() {
  digitalWrite(PIN_IN1, LOW);
  digitalWrite(PIN_IN2, LOW);
  digitalWrite(PIN_IN3, LOW);
  digitalWrite(PIN_IN4, LOW);
  if (PIN_ENA != -1) analogWrite(PIN_ENA, 0);
  if (PIN_ENB != -1) analogWrite(PIN_ENB, 0);
  isMoving = false;
}

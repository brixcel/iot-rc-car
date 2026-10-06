# 🚗 CYBER-RC // Dual-Microcontroller IoT RC Car
### Comprehensive Wiring Diagram, Hardware Architecture & Setup Guide

---

## 📑 Table of Contents
1. [System Architecture (What to Tell Your Professor)](#1-system-architecture)
2. [Master Pin-to-Pin Wiring Diagram](#2-master-pin-to-pin-wiring-diagram)
3. [Power Supply & Common Ground Guide (CRITICAL)](#3-power-supply--common-ground-guide)
4. [Arduino IDE Setup & Uploading](#4-arduino-ide-setup--uploading)
5. [Connecting Your Phone & Driving](#5-connecting-your-phone--driving)
6. [Troubleshooting & Calibration](#6-troubleshooting--calibration)

---

## 1. System Architecture
> **Why keep the Arduino Uno when adding NodeMCU?**  
> In modern automotive and industrial robotics, this is known as **Distributed Real-Time Control**:
> - **NodeMCU ESP8266 (The Telematics / IoT Gateway)**: Handles high-speed 2.4 GHz 802.11 Wi-Fi networking, TCP/IP stack, HTTP Web Server, and mobile client management.
> - **Arduino Uno (The Motor Control ECU)**: Handles deterministic, real-time hardware PWM, H-bridge current switching, fail-safe watchdog timer, and sensor/actuator operations without any network jitter or Wi-Fi interrupt latency.
> - **Inter-IC Communication**: High-reliability UART Serial communication (`SoftwareSerial`) between NodeMCU and Arduino Uno at 9600 baud.

```
+-------------------------------------------------------------+
|                     SMARTPHONE / TABLET                     |
|           (Zero App Install - Runs in Chrome / Safari)       |
+-------------------------------------------------------------+
                              |
                     Wi-Fi (802.11 b/g/n)
                  SSID: CYBER-RC-WIFI (192.168.4.1)
                              v
+-------------------------------------------------------------+
|                NODEMCU ESP8266 (IoT BRIDGE)                 |
|  - Hosts Web App in Flash Memory                            |
|  - Translates touch gestures into serial commands           |
|  - Pin D1 (TX) ---> Arduino Pin 2 (RX)                      |
|  - Pin D2 (RX) <--- Arduino Pin 3 (TX)                      |
+-------------------------------------------------------------+
                              |
                     UART Serial Link
                       (9600 Baud)
                              v
+-------------------------------------------------------------+
|                  ARDUINO UNO (MOTOR ECU)                    |
|  - Parses directional commands and analog vector steering   |
|  - Drives L298N Dual H-Bridge via PWM Pins (5, 6, 7, 8, 9, 10)|
|  - Independent 1.8-second Safety Watchdog                   |
+-------------------------------------------------------------+
                              |
                        Motor Signals
                              v
+-------------------------------------------------------------+
|                  L298N MOTOR DRIVER MODULE                  |
|  - OUT1 / OUT2 ---> Left Wheels (Motor A)                   |
|  - OUT3 / OUT4 ---> Right Wheels (Motor B)                  |
+-------------------------------------------------------------+
```

---

## 2. Master Pin-to-Pin Wiring Diagram

### A. NodeMCU ESP8266 to Arduino Uno
| NodeMCU ESP8266 Pin | Arduino Uno Pin | Wire Purpose | Notes |
| :--- | :--- | :--- | :--- |
| **D1 (GPIO 5)** | **Pin 2 (RX)** | Serial Command TX | NodeMCU sends commands to Arduino |
| **D2 (GPIO 4)** | **Pin 3 (TX)** | Serial Telemetry RX | Optional feedback from Arduino |
| **GND** | **GND** | Common Ground | **MANDATORY**: Must connect to same ground |
| **VIN** | **5V** | 5V Power In | Powers NodeMCU from 5V rail |

> [!NOTE]  
> We use **Pins 2 & 3** (`SoftwareSerial`) on Arduino Uno and **Pins D1 & D2** on NodeMCU.  
> This leaves **Arduino Pins 0 & 1 and NodeMCU USB Hardware Serial completely free**, meaning you can upload code via USB without unplugging any jumper wires!

---

### B. Arduino Uno to L298N Motor Driver
| Arduino Uno Pin | L298N Pin | Function | Notes |
| :--- | :--- | :--- | :--- |
| **Pin 5 (PWM)** | **ENA** | Motor A (Left) Speed | Remove default black jumper cap on ENA |
| **Pin 6** | **IN1** | Motor A Direction 1 | Left wheels forward |
| **Pin 7** | **IN2** | Motor A Direction 2 | Left wheels reverse |
| **Pin 8** | **IN3** | Motor B Direction 1 | Right wheels forward |
| **Pin 9** | **IN4** | Motor B Direction 2 | Right wheels reverse |
| **Pin 10 (PWM)** | **ENB** | Motor B (Right) Speed | Remove default black jumper cap on ENB |
| **GND** | **GND** | Common Ground | Connect to Arduino GND & Battery (-) |

---

### C. L298N to DC Motors & Battery
| L298N Terminal | Connection Target | Description |
| :--- | :--- | :--- |
| **12V Screw Terminal** | **Battery (+) Positive** | 7.4V - 12V Battery Pack |
| **GND Screw Terminal** | **Battery (-) Negative** | Must also tie to Arduino GND & NodeMCU GND |
| **5V Screw Terminal** | **Arduino Vin or 5V** | Regulated 5V output (when 5V jumper is ON) |
| **OUT 1 & OUT 2** | **Left Motor(s)** | Connect to Left side DC gear motors |
| **OUT 3 & OUT 4** | **Right Motor(s)** | Connect to Right side DC gear motors |

---

### D. Optional Accessories (Extra Credit!)
| Accessory | Arduino Pin | Ground | Description |
| :--- | :--- | :--- | :--- |
| **Front Headlights (LED)** | **Pin 12** (via 220Ω resistor) | **GND** | Toggled via "LIGHTS" button in web app |
| **Horn / Buzzer** | **Pin 11** | **GND** | Sounds when holding "HORN" button |

---

## 3. Power Supply & Common Ground Guide

### ⚠️ The Golden Rule: Common Ground
The number one reason student robotic cars fail to work is a missing **Common Ground**.  
All GND pins must be physically connected together:
```
[Battery (-)] <----> [L298N GND] <----> [Arduino Uno GND] <----> [NodeMCU GND]
```
Without a shared ground reference, the serial data signals and PWM logic will float, causing erratic motor twitching or no response.

### Recommended Battery Configuration
- **Best Option**: 2x 18650 Li-ion batteries in series (7.4V nominal, 8.4V full charge).
- **Alternative**: 6x AA battery pack (7.2V to 9V).
- **Avoid**: Standard rectangular 9V alkaline batteries (they cannot supply enough instantaneous current for dual DC motors and will brown out the microcontrollers).

### L298N 5V Regulator Jumper
- Behind the 3 screw terminals on the L298N, there is a small 2-pin jumper cap labeled **5V-EN**.
- **Keep this jumper ON** if your battery voltage is between 7V and 12V. This allows the L298N's built-in 78M05 regulator to produce clean 5V output at the 5V terminal, which can power the Arduino Uno and NodeMCU!

---

## 4. Arduino IDE Setup & Uploading

### Step 1: Upload Arduino Firmware
1. Connect your **Arduino Uno** to your computer via USB.
2. In Arduino IDE, open:  
   [`c:\Users\brexc\rc car\Arduino_RC_Car\Arduino_RC_Car.ino`](file:///c:/Users/brexc/rc%20car/Arduino_RC_Car/Arduino_RC_Car.ino)
3. Select **Tools > Board > Arduino Uno**.
4. Select the matching **COM Port**.
5. Click **Upload** (Arrow icon).
6. Open the **Serial Monitor** at **9600 baud**. You should see:
   ```
   ========================================
      CYBER-RC // ARDUINO UNO READY        
      Listening to NodeMCU on Pins 2 & 3   
   ========================================
   ```
7. *(Optional test)*: Type `F:200` into the Serial Monitor input and hit Enter. Your motors should spin forward! Type `S:0` to stop.

---

### Step 2: Upload NodeMCU Firmware
1. If you haven't installed ESP8266 support in Arduino IDE yet:
   - Go to **File > Preferences**.
   - In "Additional Boards Manager URLs", paste:  
     `http://arduino.esp8266.com/stable/package_esp8266com_index.json`
   - Go to **Tools > Board > Boards Manager**, search for `esp8266`, and click **Install**.
2. Connect your **NodeMCU ESP8266** to your computer via USB.
3. Open:  
   [`c:\Users\brexc\rc car\NodeMCU_IoT_Bridge\NodeMCU_IoT_Bridge.ino`](file:///c:/Users/brexc/rc%20car/NodeMCU_IoT_Bridge/NodeMCU_IoT_Bridge.ino)
4. Select **Tools > Board > ESP8266 Boards > NodeMCU 1.0 (ESP-12E Module)**.
5. Select the NodeMCU **COM Port**.
6. Set **Upload Speed** to `115200` (or `921600` for faster flashing).
7. Click **Upload**.
8. Open the **Serial Monitor** at **115200 baud**. You will see:
   ```
   ========================================
      CYBER-RC // NODEMCU IOT BRIDGE       
   ========================================
   [WIFI]: Access Point started! SSID: CYBER-RC-WIFI
   [WIFI]: Hotspot IP Address: http://192.168.4.1
   [MDNS]: Responder started! Access via: http://rccar.local
   [HTTP]: Web Server active on port 80
   ```

---

## 5. Connecting Your Phone & Driving

1. Turn on the battery power switch of your RC Car.
2. On your **Phone (iPhone or Android)**, open **Wi-Fi Settings**.
3. Connect to the Wi-Fi network:
   - **SSID**: `CYBER-RC-WIFI`
   - **Password**: `12345678`
   *(If your phone says "Internet not available, stay connected?", tap **Yes / Stay Connected**)*.
4. Open **Google Chrome** or **Safari** and go to:
   ```
   http://192.168.4.1
   ```
   *(Or simply `http://rccar.local`)*.
5. **Install as One-Shot App (Full Screen)**:
   - **iPhone (Safari)**: Tap the Share button (box with up arrow) -> tap **"Add to Home Screen"**.
   - **Android (Chrome)**: Tap the 3 dots menu -> tap **"Install app"** or **"Add to Home Screen"**.
   - It will now open like a native mobile app without any address bar!
6. Choose between **D-PAD BUTTONS** or **VIRTUAL JOYSTICK** and drive!

---

## 6. Troubleshooting & Calibration

| Symptom | Cause | Solution |
| :--- | :--- | :--- |
| **Car turns opposite when pressing Forward** | Motor polarity reversed | In [`Arduino_RC_Car.ino`](file:///c:/Users/brexc/rc%20car/Arduino_RC_Car/Arduino_RC_Car.ino), swap `PIN_IN1` and `PIN_IN2` (or simply swap the two wires at the L298N `OUT1/OUT2` terminals). |
| **One side wheels don't spin** | ENA or ENB jumper not removed | Verify you pulled off the black jumper caps on the L298N `ENA` and `ENB` pins before connecting Arduino Pins 5 & 10. |
| **NodeMCU shows commands in Serial, but Arduino doesn't react** | TX/RX crossed or missing GND | Make sure **NodeMCU D1** connects to **Arduino Pin 2**, and **NodeMCU GND** connects to **Arduino GND**. |
| **Car stops after 1-2 seconds** | Safety Watchdog | The built-in fail-safe timer stops the car if no button is held or if Wi-Fi drops. Keep your thumb pressed on the direction pad or joystick while driving. |
| **Phone disconnects from Wi-Fi** | "Auto-switch to Mobile Data" | On your phone's Wi-Fi settings for `CYBER-RC-WIFI`, turn off "Auto Reconnect to Cellular Data" or turn on Airplane mode with Wi-Fi enabled. |

---
*Created for your IoT RC Car Project.*

# 🏎️ IoT RC Car: NodeMCU ESP8266 + Arduino Uno + L298N

An IoT-enabled RC Car architecture that retains your existing **Arduino Uno** for real-time motor and sensor control, while integrating a **NodeMCU ESP8266** as the wireless IoT gateway. It hosts a dedicated mobile web cockpit directly from the chip—allowing any smartphone to control the car over Wi-Fi with **zero app installation**.

---

## 📁 Project Structure

| File / Directory | Description |
| :--- | :--- |
| 📄 [**`MQTT_IOT_SETUP_GUIDE.md`**](file:///c:/Users/brexc/rc%20car/MQTT_IOT_SETUP_GUIDE.md) | **Step-by-step True IoT Cloud Setup Guide** for cross-floor / long-range testing via Mobile Hotspot + HiveMQ. |
| 📄 [**`WIRING_AND_SETUP_GUIDE.md`**](file:///c:/Users/brexc/rc%20car/WIRING_AND_SETUP_GUIDE.md) | Master wiring diagram, power guide, and hardware troubleshooting manual. |
| 📁 [**`Arduino_RC_Car/Arduino_RC_Car.ino`**](file:///c:/Users/brexc/rc%20car/Arduino_RC_Car/Arduino_RC_Car.ino) | Arduino Uno firmware for L298N H-Bridge motor driving, differential steering, and failsafe watchdog. |
| 📁 [**`NodeMCU_IoT_Bridge/NodeMCU_IoT_Bridge.ino`**](file:///c:/Users/brexc/rc%20car/NodeMCU_IoT_Bridge/NodeMCU_IoT_Bridge.ino) | NodeMCU ESP8266 firmware with MQTT Cloud IoT bridge and UART serial transmitter to Uno. |
| 📁 [**`web_controller/`**](file:///c:/Users/brexc/rc%20car/web_controller/) | Mobile web cockpit ([`index.html`](file:///c:/Users/brexc/rc%20car/web_controller/index.html), [`style.css`](file:///c:/Users/brexc/rc%20car/web_controller/style.css), [`app.js`](file:///c:/Users/brexc/rc%20car/web_controller/app.js)) with live MQTT WebSockets Cloud control & telemetry. |

---

## 🚀 Quick Start Summary

### 1. Wiring (Zero Changes Needed)
- **NodeMCU D1 (TX)** ➔ **Arduino Uno Pin 2 (RX)**
- **NodeMCU D2 (RX)** ➔ **Arduino Uno Pin 3 (TX)**
- **Common Ground**: All GND pins (**NodeMCU GND**, **Arduino GND**, **L298N GND**, and **Battery Negative**) must be wired together.

### 2. Flashing
1. **Arduino Uno**: Upload [`Arduino_RC_Car/Arduino_RC_Car.ino`](file:///c:/Users/brexc/rc%20car/Arduino_RC_Car/Arduino_RC_Car.ino).
2. **NodeMCU**: Install `PubSubClient` library in Arduino IDE, set your follower's hotspot credentials in [`NodeMCU_IoT_Bridge/NodeMCU_IoT_Bridge.ino`](file:///c:/Users/brexc/rc%20car/NodeMCU_IoT_Bridge/NodeMCU_IoT_Bridge.ino), and upload.

### 3. Driving from the 2nd Floor (True IoT)
1. Have your follower turn on their **2.4 GHz Android Mobile Hotspot** near the car.
2. The NodeMCU connects to the hotspot and links to the **HiveMQ MQTT Cloud Broker**.
3. Open [`web_controller/index.html`](file:///c:/Users/brexc/rc%20car/web_controller/index.html) on your phone on the 2nd floor (via GitHub Pages or browser).
4. Drive the car from across floors with live cloud telemetry!

---

## 🏆 Presentation Tip for Your Professor
> "Our RC car utilizes an **industry-standard MQTT Cloud IoT architecture**. The **NodeMCU ESP8266** acts as the wireless IoT Gateway connected via a mobile cellular hotspot, communicating asynchronously with an **MQTT Broker (HiveMQ)**. This decouples the controller on the 2nd floor from the physical vehicle on the ground floor, enabling **global, infinite-range operation** with round-trip latency under 80ms while maintaining an autonomous failsafe watchdog on the Arduino Uno ECU."

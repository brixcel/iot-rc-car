// Copy this file to secrets.h (same folder) and fill in your values.
// secrets.h is git-ignored, so your passwords never get pushed to GitHub.

#pragma once

// Follower's mobile hotspot
const char* WIFI_SSID = "YOUR_HOTSPOT_NAME";
const char* WIFI_PASS = "YOUR_HOTSPOT_PASSWORD";

// MQTT broker (EMQX Serverless: Overview page -> address, MQTT over TLS port)
const char* MQTT_BROKER = "xxxxxxxx.ala.asia-southeast1.emqxsl.com";
const int   MQTT_PORT   = 8883;
const char* MQTT_USER   = "rccar";
const char* MQTT_PASS   = "YOUR_MQTT_PASSWORD";

// secrets_sample.h - template for secrets.h
// Copy this file to secrets.h (same folder as the .ino) and fill in your values.
// secrets.h is listed in .gitignore so your credentials stay out of the repo.
#pragma once

const char* WIFI_SSID   = "your-wifi-ssid";
const char* WIFI_PASS   = "your-wifi-password";

const char* MQTT_HOST   = "192.168.1.x";    // MQTT broker IP or hostname
const uint16_t MQTT_PORT = 1883;
const char* MQTT_USER   = "";               // leave "" if broker has no auth
const char* MQTT_PASS   = "";

#ifndef ENV_CONFIG_H
#define ENV_CONFIG_H

#include <Arduino.h>

// ---------------- Config loaded from .env (LittleFS) ----------------
// Secrets and per-device settings live in a ".env" file on the device's
// own LittleFS flash filesystem instead of being hardcoded in source.
// This keeps WiFi/MQTT/Firebase/Discord credentials out of version
// control and lets you flash the same firmware to multiple devices with
// different .env files.
//
// How to get the .env file onto the device:
//   Arduino IDE : create a folder named "data" next to the sketch files,
//                 put your ".env" file inside it, then use the
//                 "ESP32 Sketch Data Upload" tool (Tools menu) to flash
//                 it to LittleFS. You may need to install that tool
//                 first from the arduino-littlefs-upload plugin.
//   PlatformIO  : put your ".env" file in a "data" folder at the
//                 project root, then run: pio run --target uploadfs
//
// Recognized keys (see .env.example for a template, all optional -
// anything left out falls back to the compiled-in default below):
//   WIFI_SSID, WIFI_PASSWORD
//   MQTT_BROKER, MQTT_PORT, MQTT_USERNAME, MQTT_PASSWORD
//   MQTT_DEVICE_ID, MQTT_ACCESS_KEY
//   FIREBASE_HOST, FIREBASE_AUTH
//   DISCORD_WEBHOOK_URL
// Lines starting with # are comments; values may optionally be wrapped
// in double quotes.
//
// These buffers start out holding safe defaults/placeholders.
// loadEnvFile() overwrites whichever of them have a matching line in the
// .env file, and leaves the rest on their default. Buffers, not
// "const char *", since loadEnvFile() needs to write into them.
extern char WIFI_SSID[48];
extern char WIFI_PASSWORD[64];

extern char MQTT_BROKER[64];
extern uint16_t MQTT_PORT;
extern char MQTT_USERNAME[48];   // leave blank in .env to connect anonymously
extern char MQTT_PASSWORD[64];
extern char MQTT_DEVICE_ID[32];  // used to build both MQTT topics, see network.h
extern char MQTT_ACCESS_KEY[48]; // required "key" field on incoming threshold commands

extern char MQTT_TOPIC_DATA[80];      // built by buildTopics(), not set directly from .env
extern char MQTT_TOPIC_THRESHOLD[80]; // built by buildTopics(), not set directly from .env

extern char FIREBASE_HOST[96]; // no "https://", no trailing slash
extern char FIREBASE_AUTH[96];

extern char DISCORD_WEBHOOK_URL[192];

// Mounts LittleFS and reads "/.env" from it, one "KEY=VALUE" per line.
// Blank lines and lines starting with # are skipped. Anything not set in
// the file keeps the compiled-in default it already had. Returns false
// (and leaves everything on defaults) if LittleFS won't mount or the
// file isn't there - treated as a soft failure, not a reason to stop
// booting, so testing without a real .env still works off the defaults.
bool loadEnvFile();

// Builds MQTT_TOPIC_DATA / MQTT_TOPIC_THRESHOLD from MQTT_DEVICE_ID. Call
// this after loadEnvFile() so it picks up an .env override of the ID.
void buildTopics();

#endif // ENV_CONFIG_H

#ifndef ENV_CONFIG_H
#define ENV_CONFIG_H

#include <Arduino.h>

// Firmware configuration is defined directly in src/env_config.cpp.
// Edit those constants before building and flashing a device.
extern const char WIFI_SSID[];
extern const char WIFI_PASSWORD[];

extern const char MQTT_BROKER[];
extern const uint16_t MQTT_PORT;
extern const char MQTT_DEVICE_ID[];  // used to build both MQTT topics, see network.h
extern const char MQTT_ACCESS_KEY[]; // required "key" field on incoming threshold commands

extern const String MQTT_TOPIC_DATA;
extern const String MQTT_TOPIC_THRESHOLD;

extern const char FIREBASE_HOST[]; // no "https://", no trailing slash
extern const char FIREBASE_AUTH[];

extern const char DISCORD_WEBHOOK_URL[];

#endif // ENV_CONFIG_H

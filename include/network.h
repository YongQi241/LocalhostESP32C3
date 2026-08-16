#ifndef NETWORK_H
#define NETWORK_H

#include <Arduino.h>
#include <WiFi.h>

// PubSubClient defaults to a 256 byte MQTT packet limit. A "zones" update can exceed that, so raise it before including the header.
#define MQTT_MAX_PACKET_SIZE 512
#include <PubSubClient.h>

extern WiFiClient wifiClient;
extern PubSubClient mqttClient;

// Your purpose is to blink LED
extern bool settingsChanged;

/*  
*   Topics are derived from MQTT_DEVICE_ID as xiao/esp32c3/sensors/<MQTT_DEVICE_ID> 
*   Incoming mqtt must have a MQTT_ACCESS_KEY, else silently ignored. 
*   Live threshold/awake/zones updates are published (retained) to MQTT_TOPIC_THRESHOLD as JSON, e.g.: {"key":"MQTT_ACCESS_KEY","distance_mm":50,"light":100,"awake_ms":5000}
*/

bool connectWiFi();
bool connectMQTT();
void mqttCallback(char *topic, byte *payload, unsigned int length);

// Publishes distance, light, and switch state as a retained reading.
void publishReadingsMQTT(uint16_t distanceMM, int lightRaw, bool switchOn, const char *state, const char *sensorStatus);

#endif // NETWORK_H

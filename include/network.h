#ifndef NETWORK_H
#define NETWORK_H

#include <Arduino.h>
#include <WiFi.h>

// PubSubClient defaults to a 256 byte MQTT packet limit. A "zones" update
// with several entries can exceed that, so raise it before including the
// header (must be defined before the #include, PubSubClient only applies
// it if nothing else set it first). Every file that includes network.h
// gets PubSubClient.h pulled in with this same limit already applied.
#define MQTT_MAX_PACKET_SIZE 512
#include <PubSubClient.h>

extern WiFiClient wifiClient;
extern PubSubClient mqttClient;

// Set by mqttCallback() whenever a threshold/awake setting actually
// changes; consumed (and cleared) in main.cpp's runActiveWindow() to
// trigger the confirmation blink. Plain global is fine - it only needs
// to live within a single wake cycle.
extern bool settingsChanged;

// ---------------- MQTT access control ----------------
// A public broker like broker.hivemq.com has no per-client login, so
// anyone can technically subscribe to any topic. Two layers guard
// against that here:
//   a) Topic scoping - both topics are built at runtime (see
//      env_config.h's buildTopics()) as
//        xiao/esp32c3/sensors/<MQTT_DEVICE_ID>/data
//        xiao/esp32c3/sensors/<MQTT_DEVICE_ID>/threshold
//      Only someone who knows your MQTT_DEVICE_ID (kept in .env, not in
//      source) knows which topic to subscribe to.
//   b) Shared key - every outgoing data message carries your device ID
//      and MQTT_ACCESS_KEY, so a receiver can verify the message really
//      came from your device. Every incoming threshold/settings command
//      MUST include a matching "key" field or it is silently ignored -
//      this stops randoms on the public broker from reconfiguring your
//      device even if they guess the topic name.
// If MQTT_USERNAME/MQTT_PASSWORD are also set in .env, connectMQTT()
// logs in instead of connecting anonymously - use this if you switch to
// a private broker that supports per-user accounts, the strongest way to
// restrict who can connect at all.
//
// Live threshold/awake/zones updates are published (retained) to
// MQTT_TOPIC_THRESHOLD as JSON, any subset of fields is fine, e.g.:
//   {"key":"YOUR_MQTT_ACCESS_KEY","distance_mm":50,"light":100,"awake_ms":5000}
// The LED blinks 3 times to confirm the device picked up the change.

bool connectWiFi();
bool connectMQTT();
void mqttCallback(char *topic, byte *payload, unsigned int length);

// Publishes distance+light to MQTT_TOPIC_DATA, retained, so a client
// that connects after this fires still receives the last known reading
// instead of nothing until the next wake.
void publishReadingsMQTT(uint16_t distanceMM, int lightRaw);

#endif // NETWORK_H

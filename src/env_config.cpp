#include "env_config.h"

// Need changing on physical device
const char WIFI_SSID[] = "Wokwi-GUEST";
const char WIFI_PASSWORD[] = "";

const char MQTT_BROKER[] = "broker.hivemq.com";
const uint16_t MQTT_PORT = 1883;

// Change for a bit of security
const char MQTT_DEVICE_ID[] = "xiao01";
const char MQTT_ACCESS_KEY[] = "";

const String MQTT_TOPIC_DATA = String("xiao/esp32c3/sensors/") + MQTT_DEVICE_ID + "/data";
const String MQTT_TOPIC_THRESHOLD = String("xiao/esp32c3/sensors/") + MQTT_DEVICE_ID + "/threshold";

// Replace with actual hooks and token
const char FIREBASE_HOST[] = "YOUR_PROJECT-default-rtdb.firebaseio.com";
const char FIREBASE_AUTH[] = "YOUR_DATABASE_SECRET_OR_ID_TOKEN";

const char DISCORD_WEBHOOK_URL[] = "https://discord.com/api/webhooks/YOUR_WEBHOOK_ID/YOUR_WEBHOOK_TOKEN";

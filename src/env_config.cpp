#include "env_config.h"
#include <FS.h>
#include <LittleFS.h>

char WIFI_SSID[48]      = "Wokwi-GUEST";
char WIFI_PASSWORD[64]  = "";

char MQTT_BROKER[64]    = "broker.hivemq.com";
uint16_t MQTT_PORT      = 1883;
char MQTT_USERNAME[48]  = "";
char MQTT_PASSWORD[64]  = "";
char MQTT_DEVICE_ID[32] = "xiao01";
char MQTT_ACCESS_KEY[48]= "";

char MQTT_TOPIC_DATA[80]      = "";
char MQTT_TOPIC_THRESHOLD[80] = "";

char FIREBASE_HOST[96] = "YOUR_PROJECT-default-rtdb.firebaseio.com";
char FIREBASE_AUTH[96] = "YOUR_DATABASE_SECRET_OR_ID_TOKEN";

char DISCORD_WEBHOOK_URL[192] = "https://discord.com/api/webhooks/YOUR_WEBHOOK_ID/YOUR_WEBHOOK_TOKEN";

// Applies one "KEY=VALUE" line from .env to the matching global above.
// Unknown keys are logged and skipped rather than treated as an error, so
// an .env file can carry comments/extra keys without breaking anything.
static void applyEnvLine(const String &key, const String &value) {
  if (key == "WIFI_SSID") strlcpy(WIFI_SSID, value.c_str(), sizeof(WIFI_SSID));
  else if (key == "WIFI_PASSWORD") strlcpy(WIFI_PASSWORD, value.c_str(), sizeof(WIFI_PASSWORD));
  else if (key == "MQTT_BROKER") strlcpy(MQTT_BROKER, value.c_str(), sizeof(MQTT_BROKER));
  else if (key == "MQTT_PORT") MQTT_PORT = (uint16_t)value.toInt();
  else if (key == "MQTT_USERNAME") strlcpy(MQTT_USERNAME, value.c_str(), sizeof(MQTT_USERNAME));
  else if (key == "MQTT_PASSWORD") strlcpy(MQTT_PASSWORD, value.c_str(), sizeof(MQTT_PASSWORD));
  else if (key == "MQTT_DEVICE_ID") strlcpy(MQTT_DEVICE_ID, value.c_str(), sizeof(MQTT_DEVICE_ID));
  else if (key == "MQTT_ACCESS_KEY") strlcpy(MQTT_ACCESS_KEY, value.c_str(), sizeof(MQTT_ACCESS_KEY));
  else if (key == "FIREBASE_HOST") strlcpy(FIREBASE_HOST, value.c_str(), sizeof(FIREBASE_HOST));
  else if (key == "FIREBASE_AUTH") strlcpy(FIREBASE_AUTH, value.c_str(), sizeof(FIREBASE_AUTH));
  else if (key == "DISCORD_WEBHOOK_URL") strlcpy(DISCORD_WEBHOOK_URL, value.c_str(), sizeof(DISCORD_WEBHOOK_URL));
  else Serial.printf("Unknown .env key ignored: %s\n", key.c_str());
}

bool loadEnvFile() {
  if (!LittleFS.begin(true)) { // true = format the partition if it's corrupt/unformatted
    Serial.println(".env: LittleFS mount failed, using compiled-in defaults.");
    return false;
  }

  File f = LittleFS.open("/.env", "r");
  if (!f) {
    Serial.println(".env: file not found on LittleFS, using compiled-in defaults.");
    return false;
  }

  while (f.available()) {
    String line = f.readStringUntil('\n');
    line.trim();
    if (line.length() == 0 || line.startsWith("#")) continue;

    const int eq = line.indexOf('=');
    if (eq < 0) continue; // malformed line, skip it rather than aborting the whole file

    String key = line.substring(0, eq);
    String value = line.substring(eq + 1);
    key.trim();
    value.trim();

    if (value.length() >= 2 && value.startsWith("\"") && value.endsWith("\"")) {
      value = value.substring(1, value.length() - 1);
    }

    applyEnvLine(key, value);
  }

  f.close();
  Serial.println(".env: loaded from LittleFS.");
  return true;
}

void buildTopics() {
  snprintf(MQTT_TOPIC_DATA, sizeof(MQTT_TOPIC_DATA),
      "xiao/esp32c3/sensors/%s/data", MQTT_DEVICE_ID);
  snprintf(MQTT_TOPIC_THRESHOLD, sizeof(MQTT_TOPIC_THRESHOLD),
      "xiao/esp32c3/sensors/%s/threshold", MQTT_DEVICE_ID);
}

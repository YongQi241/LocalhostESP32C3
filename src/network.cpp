#include "network.h"
#include "config.h"
#include "env_config.h"
#include "state.h"
#include "zones.h"
#include <ArduinoJson.h>
#include <esp_system.h>

WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);
bool settingsChanged = false;

bool connectWiFi()
{
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  const uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_CONNECT_TIMEOUT_MS)
  {
    delay(100);
  }

  if (WiFi.status() == WL_CONNECTED)
  {
    Serial.print("WiFi connected, IP: ");
    Serial.println(WiFi.localIP());
    return true;
  }

  Serial.println("WiFi connect failed/timeout.");
  return false;
}

bool connectMQTT()
{
  mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
  mqttClient.setCallback(mqttCallback);
  mqttClient.setSocketTimeout(3);

  const String clientId = "xiao-c3-" + String(esp_random(), HEX);

  const uint32_t start = millis();
  while (!mqttClient.connected() && millis() - start < MQTT_CONNECT_TIMEOUT_MS)
  {
    const bool ok = mqttClient.connect(clientId.c_str());
    if (ok)
    {
      Serial.println("MQTT connected.");
      return true;
    }
    delay(200);
  }

  Serial.print("MQTT connect failed, state=");
  Serial.println(mqttClient.state());
  return false;
}

void mqttCallback(char *topic, byte *payload, unsigned int length)
{
  if (MQTT_TOPIC_THRESHOLD != topic)
  {
    return;
  }

  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, payload, length);
  if (err)
  {
    Serial.print("Threshold message parse failed: ");
    Serial.println(err.c_str());
    return;
  }

  // Only accept command if it carries the correct key else nothing shall change.
  // If MQTT_ACCESS_KEY is blank, skip.
  if (MQTT_ACCESS_KEY[0] != '\0')
  {
    const char *providedKey = doc["key"] | "";
    if (strcmp(providedKey, MQTT_ACCESS_KEY) != 0)
    {
      Serial.println("Threshold message rejected: missing or incorrect access key.");
      return;
    }
  }

  bool changed = false;

  if (doc["distance_mm"].is<uint16_t>())
  {
    const uint16_t v = doc["distance_mm"].as<uint16_t>();
    if (v != distanceThresholdMM)
    {
      distanceThresholdMM = v;
      changed = true;
    }
  }
  if (doc["light"].is<int>())
  {
    const int v = doc["light"].as<int>();
    if (v != lightThreshold)
    {
      lightThreshold = v;
      changed = true;
    }
  }
  if (doc["awake_ms"].is<uint32_t>())
  {
    const uint32_t v = doc["awake_ms"].as<uint32_t>();
    if (v != awakeDurationMs)
    {
      awakeDurationMs = v;
      changed = true;
    }
  }
  if (doc["zones"].is<JsonArray>())
  {
    JsonArray arr = doc["zones"].as<JsonArray>();
    int newCount = 0;
    for (JsonObject z : arr)
    {
      if (newCount >= MAX_ZONES)
      {
        Serial.println("Zones message has more entries than MAX_ZONES - extra ones ignored.");
        break;
      }
      if (!z["max_mm"].is<uint16_t>() || !z["label"].is<const char *>())
      {
        continue; // skip malformed entries
      }
      zoneMaxMM[newCount] = z["max_mm"].as<uint16_t>();
      strlcpy(zoneLabel[newCount], z["label"].as<const char *>(), ZONE_LABEL_LEN);
      newCount++;
    }
    if (newCount > 0)
    {
      zoneCount = newCount;
      lastZoneIndex = -1; // re-evaluate against the new table, incase the old zone don't line up
      changed = true;
    }
    else
    {
      Serial.println("Zones message had no valid entries - table left unchanged.");
    }
  }

  if (changed)
  {
    settingsChanged = true; // consumed by runActiveWindow() to trigger the confirmation blink
    Serial.printf("Settings updated -> distance: %u mm, light: %d, awake: %lu ms\r\n", distanceThresholdMM, lightThreshold, (unsigned long)awakeDurationMs);
  }
}

void publishReadingsMQTT(uint16_t distanceMM, int lightRaw, bool switchOn)
{
  JsonDocument doc;
  doc["id"] = MQTT_DEVICE_ID;
  doc["distance_mm"] = distanceMM;
  doc["light"] = lightRaw;
  doc["switch_on"] = switchOn;
  doc["wake"] = wakeCount;

  String payload;
  serializeJson(doc, payload);

  // the broker holds the message and hands it to the next subscriber, even long afterpublish --> a late connection can sees the recent reading instead of nothing
  if (mqttClient.publish(MQTT_TOPIC_DATA.c_str(), payload.c_str(), true))
  {
    Serial.print("MQTT published: ");
    Serial.println(payload);
  }
  else
  {
    Serial.println("MQTT publish failed.");
  }
}

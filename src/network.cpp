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

  char clientId[32];
  snprintf(clientId, sizeof(clientId), "xiao-c3-%08lx", (unsigned long)esp_random());

  // If MQTT_USERNAME is set in .env, connect with broker-level login
  // instead of anonymously - the real fix for "who can connect at all"
  // if you're on a broker that supports per-account credentials (the
  // public HiveMQ test broker used by default here does not).
  const bool useAuth = (MQTT_USERNAME[0] != '\0');

  const uint32_t start = millis();
  while (!mqttClient.connected() && millis() - start < MQTT_CONNECT_TIMEOUT_MS)
  {
    const bool ok = useAuth
                    ? mqttClient.connect(clientId, MQTT_USERNAME, MQTT_PASSWORD)
                    : mqttClient.connect(clientId);
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
  if (strcmp(topic, MQTT_TOPIC_THRESHOLD) != 0)
  {
    return;
  }

  StaticJsonDocument<512> doc; // large enough for distance_mm/light/awake_ms plus a MAX_ZONES-entry "zones" array
  const DeserializationError err = deserializeJson(doc, payload, length);
  if (err)
  {
    Serial.print("Threshold message parse failed: ");
    Serial.println(err.c_str());
    return;
  }

  /* 
  Access control: only accept this command if it carries the correct 
  shared key. Anyone can technically subscribe/publish to a topic on a
  public broker, but without this key they cannot make the device act
  on what they send. If MQTT_ACCESS_KEY is left blank in .env, the
  check is skipped
  */

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

  if (doc.containsKey("distance_mm"))
  {
    const uint16_t v = doc["distance_mm"].as<uint16_t>();
    if (v != distanceThresholdMM)
    {
      distanceThresholdMM = v;
      changed = true;
    }
  }
  if (doc.containsKey("light"))
  {
    const int v = doc["light"].as<int>();
    if (v != lightThreshold)
    {
      lightThreshold = v;
      changed = true;
    }
  }
  if (doc.containsKey("awake_ms"))
  {
    const uint32_t v = doc["awake_ms"].as<uint32_t>();
    if (v != awakeDurationMs)
    {
      awakeDurationMs = v;
      changed = true;
    }
  }
  if (doc.containsKey("zones"))
  {
    JsonArray arr = doc["zones"].as<JsonArray>();
    uint8_t newCount = 0;
    for (JsonObject z : arr)
    {
      if (newCount >= MAX_ZONES)
      {
        Serial.println("Zones message has more entries than MAX_ZONES - extra ones ignored.");
        break;
      }
      if (!z.containsKey("max_mm") || !z.containsKey("label"))
      {
        continue; // skip malformed entries rather than aborting the whole update
      }
      zoneMaxMM[newCount] = z["max_mm"].as<uint16_t>();
      strlcpy(zoneLabel[newCount], z["label"].as<const char *>(), ZONE_LABEL_LEN);
      newCount++;
    }
    if (newCount > 0)
    {
      zoneCount = newCount;
      lastZoneIndex = -1; // re-evaluate against the new table, don't assume the old zone still lines up
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
    Serial.printf("Settings updated -> distance: %u mm, light: %d, awake: %lu ms\n",
                  distanceThresholdMM, lightThreshold, (unsigned long)awakeDurationMs);
  }
}

void publishReadingsMQTT(uint16_t distanceMM, int lightRaw)
{
  // "id" and "key" let a receiver confirm the message actually came from
  // this device (matching MQTT_DEVICE_ID/MQTT_ACCESS_KEY from .env)
  // rather than from something else that guessed the topic name.
  char payload[256];
  snprintf(payload, sizeof(payload),
  "{\"id\":\"%s\",\"key\":\"%s\",\"distance_mm\":%u,\"light\":%d,\"wake\":%lu}",
  MQTT_DEVICE_ID, MQTT_ACCESS_KEY, distanceMM, lightRaw, (unsigned long)wakeCount);

  // retained=true: the broker holds this message and hands it straight
  // to the next client that subscribes, even long after this publish -
  // so an account that connects late still sees the last known reading
  // instead of nothing until the next wake.
  if (mqttClient.publish(MQTT_TOPIC_DATA, payload, true))
  {
    Serial.print("MQTT published (retained): ");
    Serial.println(payload);
  }
  else
  {
    Serial.println("MQTT publish failed.");
  }
}

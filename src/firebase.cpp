#include "firebase.h"
#include "env_config.h"
#include "state.h"
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

bool postFirebaseStatus(const char *state, bool switchOn, const char *sensorStatus, bool mqttConnected, const char *discordStatus, int maxAttempts)
{

  JsonDocument doc;
  doc["device_id"] = MQTT_DEVICE_ID;
  doc["state"] = state;
  doc["switch_on"] = switchOn;
  doc["sensor_status"] = sensorStatus;
  doc["mqtt_connected"] = mqttConnected;
  doc["discord_status"] = discordStatus;
  doc["wake"] = wakeCount;
  doc["timestamp"][".sv"] = "timestamp"; // Firebase server time in milliseconds

  String body;
  serializeJson(doc, body);

  for (int attempt = 1; attempt <= maxAttempts; attempt++)
  {
    WiFiClientSecure client;
    client.setInsecure(); // TLS certificate is not verified

    HTTPClient http;
    const String url = String("https://") + FIREBASE_HOST + "/devices/" + MQTT_DEVICE_ID + "/status.json?auth=" + FIREBASE_AUTH;

    int status = -1;
    if (http.begin(client, url))
    {
      http.addHeader("Content-Type", "application/json");
      status = http.POST(body);
      http.end();
    }
    else
    {
      Serial.println("Firebase: begin() failed.");
    }

    if (status >= 200 && status < 300)
    {
      Serial.printf("Firebase POST status: %d\n", status);
      return true;
    }

    Serial.printf("Firebase POST attempt %d/%d failed (status %d).\n", attempt, maxAttempts, status);
    if (attempt < maxAttempts)
    {
      delay(250);
    }
  }
  return false;
}

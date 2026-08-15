#include "firebase.h"
#include "env_config.h"
#include "state.h"
#include "power.h"
#include "config.h"
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

bool postFirebaseStatus(const char *state, bool switchOn, const char *sensorStatus, bool mqttConnected, const char *discordStatus, int maxAttempts)
{

  auto powerChangedByButton = []() {
    return handlePowerButton();
  };

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
    if (powerChangedByButton()) return false;

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

    // HTTPClient::POST is synchronous, so check immediately when it returns.
    if (powerChangedByButton()) return false;

    if (status >= 200 && status < 300)
    {
      Serial.printf("Firebase POST status: %d\r\n", status);
      return true;
    }

    Serial.printf("Firebase POST attempt %d/%d failed (status %d).\r\n", attempt, maxAttempts, status);
    if (attempt < maxAttempts)
    {
      // Keep the retry pause responsive to the power button.
      const uint32_t retryStart = millis();
      while (millis() - retryStart < 250)
      {
        if (powerChangedByButton()) return false;
        delay(10);
      }
    }
  }
  return false;
}

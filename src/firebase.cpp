#include "firebase.h"
#include "env_config.h"
#include "state.h"
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

void sendToFirebase(uint16_t distanceMM, int lightRaw, bool suddenChange, bool switchOn) {
  WiFiClientSecure client;
  client.setInsecure(); // pin Firebase's root CA for production use

  HTTPClient http;
  const String url = String("https://") + FIREBASE_HOST
      + "/readings/latest.json?auth=" + FIREBASE_AUTH;

  if (!http.begin(client, url)) {
    Serial.println("Firebase: begin() failed.");
    return;
  }

  http.addHeader("Content-Type", "application/json");

  JsonDocument doc;
  doc["distance_mm"] = distanceMM;
  doc["light"] = lightRaw;
  doc["sudden_change"] = suddenChange;
  doc["switch_on"] = switchOn;
  doc["wake"] = wakeCount;

  String body;
  serializeJson(doc, body);

  const int status = http.PUT(body);
  if (status > 0) {
    Serial.printf("Firebase PUT status: %d\n", status);
  } else {
    Serial.printf("Firebase PUT failed: %s\n", http.errorToString(status).c_str());
  }

  http.end();
}

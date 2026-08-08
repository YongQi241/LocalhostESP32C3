#include "firebase.h"
#include "env_config.h"
#include "state.h"
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

void sendToFirebase(uint16_t distanceMM, int lightRaw, bool suddenChange) {
  WiFiClientSecure client;
  client.setInsecure(); // prototype-friendly; pin Firebase's root CA for production use

  HTTPClient http;
  char url[256]; // sized for the longest FIREBASE_HOST/FIREBASE_AUTH the buffers above can hold
  snprintf(url, sizeof(url),
      "https://%s/readings/latest.json?auth=%s",
      FIREBASE_HOST, FIREBASE_AUTH);

  if (!http.begin(client, url)) {
    Serial.println("Firebase: begin() failed.");
    return;
  }

  http.addHeader("Content-Type", "application/json");

  char body[192];
  snprintf(body, sizeof(body),
      "{\"distance_mm\":%u,\"light\":%d,\"sudden_change\":%s,\"wake\":%lu}",
      distanceMM, lightRaw, suddenChange ? "true" : "false", (unsigned long)wakeCount);

  const int status = http.PUT(body);
  if (status > 0) {
    Serial.printf("Firebase PUT status: %d\n", status);
  } else {
    Serial.printf("Firebase PUT failed: %s\n", http.errorToString(status).c_str());
  }

  http.end();
}

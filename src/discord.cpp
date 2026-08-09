#include "discord.h"
#include "env_config.h"
#include "zones.h"
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

void sendDiscordAlert(uint16_t distanceMM, int zoneIndex, int lightRaw, int lightDelta) {
  WiFiClientSecure client;
  client.setInsecure(); // pin Discord's root CA for production use

  HTTPClient http;
  if (!http.begin(client, DISCORD_WEBHOOK_URL)) {
    Serial.println("Discord: begin() failed.");
    return;
  }

  http.addHeader("Content-Type", "application/json");

  const String message = String(zoneLabel[zoneIndex]) + " (" + distanceMM
      + " mm) | Light: " + lightRaw + " (change " + lightDelta + ")";

  JsonDocument doc;
  doc["content"] = message;

  String content;
  serializeJson(doc, content);

  const int status = http.POST(content);
  if (status > 0) {
    Serial.printf("Discord webhook status: %d\n", status);
  } else {
    Serial.printf("Discord webhook failed: %s\n", http.errorToString(status).c_str());
  }

  http.end();
}

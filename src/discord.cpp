#include "discord.h"
#include "env_config.h"
#include "zones.h"
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

bool sendDiscordAlert(uint16_t distanceMM, int distanceDelta, int zoneIndex, int lightRaw, int lightDelta){
  WiFiClientSecure client;
  client.setInsecure(); // TLS certificate is not verified

  HTTPClient http;
  if (!http.begin(client, DISCORD_WEBHOOK_URL))
  {
    Serial.println("Discord: begin() failed.");
    return false;
  }

  http.addHeader("Content-Type", "application/json");
  const String message = String(zoneLabel[zoneIndex]) + " (" + distanceMM + " mm, change " + distanceDelta + ") | Light: " + lightRaw + " (change " + lightDelta + ")";

  JsonDocument doc;
  doc["content"] = message;

  String content;
  serializeJson(doc, content);

  const int status = http.POST(content);
  const bool ok = status >= 200 && status < 300;
  if (ok) Serial.printf("Discord status: %d\n", status);
  else Serial.printf("Discord failed (%d): %s\n", status, http.errorToString(status).c_str());

  http.end();
  return ok;
}

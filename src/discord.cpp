#include "discord.h"
#include "env_config.h"
#include "zones.h"
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

void sendDiscordAlert(uint16_t distanceMM, uint8_t zoneIndex, int lightRaw, int lightDelta) {
  WiFiClientSecure client;
  client.setInsecure(); // prototype-friendly; pin Discord's root CA for production use

  HTTPClient http;
  if (!http.begin(client, DISCORD_WEBHOOK_URL)) {
    Serial.println("Discord: begin() failed.");
    return;
  }

  http.addHeader("Content-Type", "application/json");

  char content[224];
  snprintf(content, sizeof(content),
      "{\"content\":\"%s (%u mm) | Light: %d (change %d)\"}",
      zoneLabel[zoneIndex], distanceMM, lightRaw, lightDelta);

  const int status = http.POST(content);
  if (status > 0) {
    Serial.printf("Discord webhook status: %d\n", status);
  } else {
    Serial.printf("Discord webhook failed: %s\n", http.errorToString(status).c_str());
  }

  http.end();
}

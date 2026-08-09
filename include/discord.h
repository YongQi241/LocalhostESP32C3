#ifndef DISCORD_H
#define DISCORD_H

#include <Arduino.h>

// Send to discord push notification
void sendDiscordAlert(uint16_t distanceMM, int zoneIndex, int lightRaw, int lightDelta);

#endif // DISCORD_H

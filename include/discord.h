#ifndef DISCORD_H
#define DISCORD_H

#include <Arduino.h>

// Send to discord push notification
bool sendDiscordAlert(uint16_t distanceMM, int distanceDelta, int zoneIndex, int lightRaw, int lightDelta);

#endif // DISCORD_H

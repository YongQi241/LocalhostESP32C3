#ifndef DISCORD_H
#define DISCORD_H

#include <Arduino.h>

// Sends the configured label for the given zone, plus the current light
// reading, as a single Discord webhook message - the one and only push
// in the sketch. Fires the same way whether a distance jump, a zone
// crossing, or a light jump triggered it. Swap this body out (e.g. for
// ntfy.sh or Pushover) if you want an actual phone push instead.
void sendDiscordAlert(uint16_t distanceMM, uint8_t zoneIndex, int lightRaw, int lightDelta);

#endif // DISCORD_H

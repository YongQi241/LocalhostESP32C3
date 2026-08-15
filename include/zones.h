#ifndef ZONES_H
#define ZONES_H

#include <Arduino.h>
#include "config.h"

// Distances Zones area labbeled in accending order by max_mm; last entry is treated as a catch all i.e 100 --> >100. Update is total replacement, there is MAX_ZONES
extern uint16_t zoneMaxMM[MAX_ZONES];

// zoneMaxMM[i]/zoneLabel[i] pairs, ascending by max_mm, only the first
extern char zoneLabel[MAX_ZONES][ZONE_LABEL_LEN];
extern int zoneCount;
extern int lastZoneIndex; // -1 = no valid zone has been observed yet
extern bool zoneNotificationPending;

// Returns the index of the zone the given distance falls into, or -1 if zoneCount is 0. Zones are checked in ascending order.
int zoneForDistance(uint16_t distanceMM);

#endif // ZONES_H

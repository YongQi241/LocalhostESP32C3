#ifndef ZONES_H
#define ZONES_H

#include <Arduino.h>
#include "config.h"


/* Distances Zones area labbeled in accending order by max_mm; last entry is treated as larger than itself i.e 100 --> >100 making it not very useful. 
Update is total replacement, there is maximum */
extern uint16_t zoneMaxMM[MAX_ZONES];

// zoneMaxMM[i]/zoneLabel[i] pairs, ascending by max_mm, only the first
extern char     zoneLabel[MAX_ZONES][ZONE_LABEL_LEN];
extern int zoneCount;
extern int lastZoneIndex; // -1 = unknown, forces a notification on first evaluation

// Returns the index of the zone the given distance falls into, or -1 if zoneCount is 0. Zones are checked in ascending order.
int zoneForDistance(uint16_t distanceMM);

#endif // ZONES_H

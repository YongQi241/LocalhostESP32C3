#ifndef ZONES_H
#define ZONES_H

#include <Arduino.h>
#include "config.h"

// ---------------- Distance zones (live configurable over MQTT) ----------------
// Distance zones with their own label (e.g. "Close!" under 30cm, "Far.."
// beyond that) can be changed live, without reflashing, on the threshold
// topic (see network.h) by adding a "zones" array to the message:
//   {"zones":[{"max_mm":300,"label":"Close!"},
//             {"max_mm":600,"label":"Medium"},
//             {"max_mm":65535,"label":"Far.."}]}
// Zones must be listed in ascending order by max_mm - the last entry in
// the list is treated as the catch-all for anything beyond it, so its
// max_mm value barely matters as long as it is the biggest one. Up to
// MAX_ZONES zones are kept; sending fewer zones than that simply replaces
// the whole table with the shorter one.
//
// zoneMaxMM[i]/zoneLabel[i] pairs, ascending by max_mm, only the first
// zoneCount entries are valid. RTC_DATA_ATTR (in zones.cpp) so the table
// survives deep sleep. Default matches the classic "Close!"/"Far.."
// example: <=300mm is Close!, anything past that is Far..
extern uint16_t zoneMaxMM[MAX_ZONES];
extern char     zoneLabel[MAX_ZONES][ZONE_LABEL_LEN];
extern uint8_t  zoneCount;
extern int8_t   lastZoneIndex; // -1 = unknown, forces a notification on first evaluation

// Returns the index of the zone the given distance falls into, or -1 if
// zoneCount is somehow 0. Zones are checked in ascending order; the last
// configured zone always matches anything that didn't match an earlier
// one, so it acts as the catch-all regardless of its own max_mm value.
int8_t zoneForDistance(uint16_t distanceMM);

#endif // ZONES_H

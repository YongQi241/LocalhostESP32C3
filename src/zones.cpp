#include "zones.h"

RTC_DATA_ATTR uint16_t zoneMaxMM[MAX_ZONES] = {300, 65535, 0, 0, 0};
RTC_DATA_ATTR char zoneLabel[MAX_ZONES][ZONE_LABEL_LEN] = {"Close!", "Far..", "", "", ""};
RTC_DATA_ATTR int zoneCount = 2;
RTC_DATA_ATTR int lastZoneIndex = -1;

int zoneForDistance(uint16_t distanceMM)
{
  for (int i = 0; i < zoneCount; i++)
  {
    if (i == zoneCount - 1 || distanceMM <= zoneMaxMM[i])
    {
      return i;
    }
  }
  return -1;
}

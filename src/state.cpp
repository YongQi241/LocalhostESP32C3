#include "state.h"

RTC_DATA_ATTR uint16_t lastDistanceMM = 0;
RTC_DATA_ATTR int lastLightRaw = 0;
RTC_DATA_ATTR bool hasBaseline = false;
RTC_DATA_ATTR uint16_t distanceThresholdMM = 100; // default sudden: delta>100mm
RTC_DATA_ATTR int lightThreshold = 200;           // default sudden: delta>200
RTC_DATA_ATTR uint32_t awakeDurationMs = 3000;    // stay awake this long after the LAST sudden
RTC_DATA_ATTR uint32_t wakeCount = 0;
RTC_DATA_ATTR bool switchOffReported = false;

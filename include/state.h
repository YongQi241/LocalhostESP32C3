#ifndef STATE_H
#define STATE_H

#include <Arduino.h>

// ---------------- Persisted across deep-sleep cycles ----------------
// Deep sleep resets the chip, so ordinary globals get wiped - these are
// defined with the RTC_DATA_ATTR storage attribute in state.cpp, which
// keeps them in RTC memory across the reset. Declared here as plain
// externs so any file can read/update them without repeating the
// attribute (only the one defining translation unit needs it).

extern uint16_t lastDistanceMM;      // last distance reading, used for delta
extern int      lastLightRaw;        // last light reading, used for delta
extern bool     hasBaseline;         // false until the first reading has been taken
extern uint16_t distanceThresholdMM; // live-configurable "sudden" for distance
extern int      lightThreshold;      // live-configurable "sudden" for light
extern uint32_t awakeDurationMs;     // live-configurable idle timeout for the active window
extern uint32_t wakeCount;           // increments every wake, used for the heartbeat and "wake" field
extern bool     switchOffReported;   // prevents duplicate OFF reports on timer wakes

#endif // STATE_H

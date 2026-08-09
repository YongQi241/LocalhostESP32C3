#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// ---------------- Pin configuration ----------------
constexpr int LIGHT_PIN  = D2;  // TEMT6000 OUT
constexpr int SWITCH_PIN = D3;  // Slide-switch center (RTC-capable, enables ext0 wakeup)
constexpr int LED_PIN    = D10;

// ---------------- Timing ----------------
constexpr uint64_t CHECK_INTERVAL_US       = 2ULL * 1000000ULL;   // poll rate while switch is ON
constexpr uint64_t SWITCH_OFF_INTERVAL_US  = 5ULL * 1000000ULL;   // slower poll while switch is OFF
constexpr uint32_t HEARTBEAT_EVERY_N_WAKES = 30;                  // ~60s @ 2s interval: sync even without a sudden change
constexpr uint32_t WIFI_CONNECT_TIMEOUT_MS = 8000;
constexpr uint32_t MQTT_CONNECT_TIMEOUT_MS = 4000;
constexpr uint32_t ACTIVE_LOOP_DELAY_MS    = 250;                 // how often we re-check sensors during the awake window
constexpr int      SETTINGS_BLINK_COUNT    = 3;                   // LED blinks to confirm a threshold/awake setting change
constexpr uint16_t SETTINGS_BLINK_MS       = 100;                 // on/off duration per blink

// ---------------- Distance zones ----------------
constexpr int      MAX_ZONES      = 5;   // cap on how many zones the "zones" MQTT field can set
constexpr int      ZONE_LABEL_LEN = 24;  // max chars per zone label, including the null terminator

#endif // CONFIG_H

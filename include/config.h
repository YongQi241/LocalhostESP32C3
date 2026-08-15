#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// ---------------- Pin configuration ----------------
constexpr int LIGHT_PIN  = D2;  // TEMT6000 OUT
constexpr int SWITCH_PIN = D3;  // Active-low pushbutton (RTC-capable, enables GPIO wakeup)
constexpr uint32_t BUTTON_HOLD_MS = 1000;
constexpr int LED_PIN    = D10;

// ---------------- Timing ----------------
constexpr uint64_t CHECK_INTERVAL_US       = 2ULL * 1000000ULL;   // poll rate while switch is ON // Current 2 seconds // microseconds
constexpr uint64_t SWITCH_OFF_INTERVAL_US  = 5ULL * 1000000ULL;   // slower poll while switch is OFF // Currently 3 seconds // microseconds
constexpr uint32_t HEARTBEAT_EVERY_N_WAKES = 30;                  // ~60s @ 2s interval: sync even without a sudden change // Are you alive ? Call me as soon as you are awake every 30 days
constexpr uint32_t WIFI_CONNECT_TIMEOUT_MS = 8000;
constexpr uint32_t MQTT_CONNECT_TIMEOUT_MS = 4000;
constexpr uint32_t ACTIVE_LOOP_DELAY_MS    = 250;                 // how often sensors is checked during the awake window
constexpr uint32_t FIREBASE_STATUS_INTERVAL_MS = 1000;            // append device status while awake
constexpr int      FIREBASE_FINAL_RETRY_COUNT = 3;
constexpr int      SETTINGS_BLINK_COUNT    = 3;                   // LED blinks to confirm a threshold/awake setting change
constexpr uint16_t SETTINGS_BLINK_MS       = 100;                 // on/off duration per blink

// ---------------- Distance zones ----------------
constexpr int      MAX_ZONES      = 5;   // cap on how many zones the "zones" MQTT field can set
constexpr int      ZONE_LABEL_LEN = 24;  // max chars per zone label, including the null terminator

#endif // CONFIG_H

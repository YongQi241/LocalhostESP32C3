/*
 * XIAO ESP32-C3 + VL53L1X distance + TEMT6000 light sensor
 * -----------------------------------------------------------------
 * Behavior implemented:
 *   1. Sends both readings (distance + light) over MQTT to a broker
 *      ("server"), retained so a late-connecting client still gets the
 *      last known reading. See network.h for topic scoping and the
 *      shared-key access control.
 *   2. Sends both readings to Firebase Realtime Database ("cloud").
 *   3. Deep-sleeps and only does the network work above when a "sudden
 *      change" in either reading is detected (or on a periodic heartbeat,
 *      see HEARTBEAT_EVERY_N_WAKES in config.h, so it never goes fully
 *      silent).
 *   4. Sends a Discord webhook push whenever a sudden change (distance or
 *      light) or a zone crossing fires. The message is always the current
 *      distance zone's label plus the distance, with the light reading
 *      folded in too (e.g. "Close! (280 mm) | Light: 512 (change 220)") -
 *      there's no separate light-only message. The push is sent first,
 *      before the MQTT publish and the Firebase write, so it goes out
 *      with the least delay. See zones.h for how zones work.
 *   5. If the slide switch is OFF, the device skips sensors/network
 *      entirely and just polls the switch at a slower interval.
 *   6. The LED blinks 3 times whenever a threshold OR the awake-duration
 *      setting is changed live over MQTT, as a visual acknowledgment.
 *   7. How long the device stays awake before returning to deep sleep is
 *      itself a live setting ("awake_ms") - and it's an IDLE timeout, not
 *      a fixed window: every additional sudden change resets the clock,
 *      so the device only goes back to sleep once things stop changing
 *      for awake_ms milliseconds.
 *
 * File layout:
 *   main.cpp        - this file: setup()/loop(), sensor reads, the
 *                      active-awake window, wiring everything together.
 *   config.h         - pins, timing constants, zone-table size constants.
 *   state.h/.cpp     - RTC-memory readings/thresholds that survive deep
 *                       sleep.
 *   zones.h/.cpp     - the live-configurable distance zone table.
 *   env_config.h/.cpp- loads WiFi/MQTT/Firebase/Discord config from a
 *                       ".env" file on LittleFS (see that header for the
 *                       recognized keys and upload steps).
 *   network.h/.cpp   - WiFi/MQTT connect, the settings callback, and the
 *                       ID + shared-key MQTT access control.
 *   firebase.h/.cpp  - Firebase Realtime Database write.
 *   discord.h/.cpp   - Discord webhook push.
 *   power.h/.cpp     - deep sleep entry and the LED blink helper.
 *
 * Required libraries (Library Manager):
 *   - VL53L1X by Pololu
 *   - PubSubClient by Nick O'Leary
 *   - ArduinoJson (v6.x) by Benoit Blanchon
 *   (WiFi, HTTPClient, WiFiClientSecure, LittleFS ship with the ESP32
 *   Arduino core)
 *
 * Things you MUST fill in before this is useful: copy .env.example to
 * .env, fill in real values, and upload it to LittleFS - see
 * env_config.h for exactly how.
 *
 * Hardware note: on the Seeed XIAO ESP32-C3, D6 (the switch pin) maps to
 * GPIO21, which is NOT one of the chip's RTC-capable GPIOs (only GPIO0-5
 * are). That means we can't hardware-wake the chip the instant the switch
 * flips during deep sleep - instead we just poll it on a timer, slower
 * while it's off (SWITCH_OFF_INTERVAL_US) than while it's on
 * (CHECK_INTERVAL_US).
 */

#include <Wire.h>
#include <VL53L1X.h>

#include "config.h"
#include "state.h"
#include "zones.h"
#include "env_config.h"
#include "network.h"
#include "firebase.h"
#include "discord.h"
#include "power.h"

VL53L1X distanceSensor;

// ---------------- Forward declarations ----------------
void publishAndMaybeAlert(uint16_t distanceMM, int lightRaw, int distanceDelta, int lightDelta, bool suddenChange);
void runActiveWindow();

void setup() {
  Serial.begin(115200);
  delay(100); // let USB-serial settle right after a deep-sleep reset

  pinMode(SWITCH_PIN, INPUT);
  pinMode(LED_PIN, OUTPUT);

  wakeCount++;

  const bool switchOn = digitalRead(SWITCH_PIN);

  // ---- Requirement: stop all activity while the switch is off ----
  if (!switchOn) {
    digitalWrite(LED_PIN, LOW);
    Serial.println("Switch OFF - skipping sensors/network, sleeping.");
    goToSleep(SWITCH_OFF_INTERVAL_US);
    return; // unreachable: goToSleep() resets the chip
  }

  digitalWrite(LED_PIN, HIGH);

  // ---- Load secrets/settings from .env, then build the ID-scoped topics ----
  // This runs every wake (deep sleep wipes ordinary globals, same reason
  // WiFi has to reconnect every wake too). loadEnvFile() leaves the
  // compiled-in defaults in place for anything not found in .env.
  loadEnvFile();
  buildTopics();

  // ---- Read sensors ----
  Wire.begin(D4, D5); // VL53L1X: SDA=D4, SCL=D5
  distanceSensor.setTimeout(500);

  uint16_t distanceMM = lastDistanceMM;
  if (distanceSensor.init()) {
    distanceSensor.setDistanceMode(VL53L1X::Long);
    distanceSensor.setMeasurementTimingBudget(50000);
    distanceSensor.startContinuous(50);
    distanceMM = distanceSensor.read(); // blocks briefly for the first sample
    if (distanceSensor.timeoutOccurred()) {
      Serial.println("VL53L1X timeout - reusing last distance.");
      distanceMM = lastDistanceMM;
    }
  } else {
    Serial.println("VL53L1X not detected this cycle - reusing last distance.");
  }

  const int lightRaw = analogRead(LIGHT_PIN);

  // ---- Decide if this is a "sudden change", and which reading caused it ----
  const int distanceDelta = hasBaseline ? abs((int)distanceMM - (int)lastDistanceMM) : 0;
  const int lightDelta    = hasBaseline ? abs(lightRaw - lastLightRaw) : 0;

  const bool distanceSuddenChange = hasBaseline && (distanceDelta > distanceThresholdMM);
  const bool lightSuddenChange    = hasBaseline && (lightDelta > lightThreshold);
  const bool suddenChange         = distanceSuddenChange || lightSuddenChange;
  const bool heartbeatDue         = (wakeCount % HEARTBEAT_EVERY_N_WAKES) == 0;
  const bool firstBoot            = !hasBaseline;

  const int8_t zoneIdx = zoneForDistance(distanceMM);
  const bool   zoneChanged  = (zoneIdx != lastZoneIndex);
  const bool   shouldNotify = suddenChange || zoneChanged; // distance jump, zone crossing, or a light jump - all get the same report

  Serial.printf(
      "Distance: %u mm (d=%d) | Light: %d (d=%d) | distSudden=%s lightSudden=%s heartbeat=%s zone=%s\n",
      distanceMM, distanceDelta, lightRaw, lightDelta,
      distanceSuddenChange ? "yes" : "no", lightSuddenChange ? "yes" : "no",
      heartbeatDue ? "yes" : "no",
      zoneIdx >= 0 ? zoneLabel[zoneIdx] : "n/a");

  if (suddenChange || heartbeatDue || firstBoot || zoneChanged) {
    if (connectWiFi() && connectMQTT()) {
      mqttClient.subscribe(MQTT_TOPIC_THRESHOLD);

      // Push notification first - it's the time critical part, so it goes
      // out before MQTT publish / Firebase logging rather than after them.
      if (shouldNotify && zoneIdx >= 0) {
        sendDiscordAlert(distanceMM, (uint8_t)zoneIdx, lightRaw, lightDelta);
        lastZoneIndex = zoneIdx;
      }

      // Handle the reading that triggered this wake. This also updates
      // lastDistanceMM/lastLightRaw, which become the baseline the active
      // window below compares against.
      publishAndMaybeAlert(distanceMM, lightRaw, distanceDelta, lightDelta, suddenChange);

      // Stay awake (duration is itself a live setting - awakeDurationMs)
      // watching for further sudden changes and settings updates instead
      // of dropping straight back to sleep.
      runActiveWindow();

      mqttClient.disconnect();
    } else {
      Serial.println("Network unavailable this cycle - will retry next wake.");
      lastDistanceMM = distanceMM;
      lastLightRaw   = lightRaw;
      hasBaseline    = true;
      // lastZoneIndex deliberately left alone here: the notification never
      // went out, so the next wake should still try to send it.
    }
  } else {
    lastDistanceMM = distanceMM;
    lastLightRaw   = lightRaw;
    hasBaseline    = true;
  }

  // The switch (or the settings) may have changed during the active window,
  // so re-check before deciding how long to sleep this time.
  const bool switchStillOn = digitalRead(SWITCH_PIN);
  goToSleep(switchStillOn ? CHECK_INTERVAL_US : SWITCH_OFF_INTERVAL_US);
}

void loop() {
  // Intentionally empty: setup() runs once per wake cycle and puts the
  // chip back into deep sleep at the end, so loop() never gets reached.
}

// Publishes the given reading to MQTT + Firebase, and updates the baseline
// used for future delta comparisons (so the same change doesn't keep
// re-triggering every loop). The Discord push itself is sent separately by
// the caller, and first - its content (the zone label with light folded in)
// doesn't depend on this function running, so there's no reason to wait on
// the MQTT publish or the Firebase write before it goes out.
void publishAndMaybeAlert(uint16_t distanceMM, int lightRaw, int distanceDelta, int lightDelta, bool suddenChange) {
  publishReadingsMQTT(distanceMM, lightRaw);
  sendToFirebase(distanceMM, lightRaw, suddenChange);

  lastDistanceMM = distanceMM;
  lastLightRaw   = lightRaw;
  hasBaseline    = true;
}

// Keeps the device connected and watching as long as sudden changes keep
// happening, and only actually times out (and returns to setup()/
// goToSleep()) after awakeDurationMs of no further sudden change - i.e.
// awakeDurationMs is an IDLE timeout, not a fixed window. Every sudden
// change resets the idle clock, so the device stays awake the whole time
// something is actively changing. Also processes incoming MQTT settings
// updates and blinks the LED to confirm any change. Returns early if the
// switch is turned off mid-window.
void runActiveWindow() {
  uint32_t lastActivity = millis(); // reset on every sudden change; window ends when this goes stale

  while (millis() - lastActivity < awakeDurationMs) {
    if (digitalRead(SWITCH_PIN) == LOW) {
      Serial.println("Switch turned OFF mid-window - stopping early.");
      digitalWrite(LED_PIN, LOW);
      return;
    }

    mqttClient.loop(); // process any incoming threshold/awake-duration updates

    if (settingsChanged) {
      Serial.println("Settings changed - blinking LED.");
      blinkLED(SETTINGS_BLINK_COUNT, SETTINGS_BLINK_MS);
      settingsChanged = false;
    }

    const uint16_t distanceMM = distanceSensor.read();
    const int lightRaw = analogRead(LIGHT_PIN);

    if (!distanceSensor.timeoutOccurred()) {
      const int distanceDelta = abs((int)distanceMM - (int)lastDistanceMM);
      const int lightDelta    = abs(lightRaw - lastLightRaw);

      const bool distanceSuddenChange = (distanceDelta > distanceThresholdMM);
      const bool lightSuddenChange    = (lightDelta > lightThreshold);
      const bool suddenChange         = distanceSuddenChange || lightSuddenChange;

      const int8_t zoneIdx = zoneForDistance(distanceMM);
      const bool   zoneChanged  = (zoneIdx != lastZoneIndex);
      const bool   shouldNotify = suddenChange || zoneChanged;

      // Push notification first, same priority order as in setup().
      if (shouldNotify && zoneIdx >= 0) {
        Serial.printf("Zone notification during active window -> %s\n", zoneLabel[zoneIdx]);
        sendDiscordAlert(distanceMM, (uint8_t)zoneIdx, lightRaw, lightDelta);
        lastZoneIndex = zoneIdx;
        lastActivity = millis(); // a zone crossing (or light jump) counts as activity too
      }

      if (suddenChange) {
        Serial.println("Additional sudden change during active window - resetting idle timer.");
        publishAndMaybeAlert(distanceMM, lightRaw, distanceDelta, lightDelta, true);
        lastActivity = millis(); // still changing - don't let the timeout run down
      }
    }

    delay(ACTIVE_LOOP_DELAY_MS);
  }
}

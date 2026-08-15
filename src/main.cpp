/*
 * Layout:
 *   main.cpp        - this file: setup()/loop(), sensor reads, the
 *                      active-awake window, wiring everything together.
 *   config.h         - pins, timing constants, zone-table size constants.
 *   state.h/.cpp     - RTC-memory readings/thresholds that survive deep
 *                       sleep.
 *   zones.h/.cpp     - the live-configurable distance zone table.
 *   env_config.h/.cpp- compiled-in WiFi/MQTT/Firebase/Discord config.
 *   network.h/.cpp   - WiFi/MQTT connect, the settings callback, and the
 *                       ID + shared-key MQTT access control.
 *   firebase.h/.cpp  - Firebase Realtime Database write.
 *   discord.h/.cpp   - Discord webhook push.
 *   power.h/.cpp     - deep sleep entry and the LED blink helper.
 *
 * TODO: edit the configuration constants in env_config.cpp.
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

void publishSensor(uint16_t distanceMM, int lightRaw);
void reportSwitchOff();
void tryReportSwitchOff();
void runActiveWindow();

const char *distanceSensorStatus = "not_checked";
const char *discordStatus = "not_sent";
bool distanceSensorAvailable = false;

void setup()
{
  Serial.begin(9600);
  delay(100); // let USB-serial settle right after a deep-sleep reset

  pinMode(SWITCH_PIN, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);

  wakeCount++;

  handlePowerButton();

  // Send one final OFF report, then stop all activity while toggled off.
  if (!deviceEnabled)
  {
    if (!switchOffReported)
      tryReportSwitchOff();
    Serial.println("Device OFF - sleeping until the next check or button press.");
    goToSleep(SWITCH_OFF_INTERVAL_US);
    return; // unreachable: goToSleep() resets the chip
  } else digitalWrite(LED_PIN, HIGH);

  // Re-arm the one-time OFF report after the device has been toggled on.
  switchOffReported = false;

  // Sensors
  Wire.begin(D4, D5); // VL53L1X: SDA=D4, SCL=D5
  distanceSensor.setTimeout(500);

  uint16_t distanceMM = lastDistanceMM;
  if (distanceSensor.init())
  {
    distanceSensorAvailable = true;
    distanceSensorStatus = "ok";
    distanceSensor.setDistanceMode(VL53L1X::Short);
    distanceSensor.setMeasurementTimingBudget(50000);
    distanceSensor.startContinuous(50);
    distanceMM = distanceSensor.read(); // blocks briefly for the first sample
    if (distanceSensor.timeoutOccurred())
    {
      distanceSensorStatus = "timeout";
      Serial.println("VL53L1X timeout - reusing last distance.");
      distanceMM = lastDistanceMM;
    }
  }
  else
  {
    distanceSensorStatus = "not_detected";
    Serial.println("VL53L1X not detected this cycle - reusing last distance.");
  }

  const int lightRaw = analogRead(LIGHT_PIN);

  // Decide if this is a "sudden change", and which reading caused it
  const int distanceDelta = hasBaseline ? abs((int)distanceMM - (int)lastDistanceMM) : 0;
  const int lightDelta = hasBaseline ? abs(lightRaw - lastLightRaw) : 0;

  const bool distanceSuddenChange = hasBaseline && (distanceDelta > distanceThresholdMM);
  const bool lightSuddenChange = hasBaseline && (lightDelta > lightThreshold);
  const bool suddenChange = distanceSuddenChange || lightSuddenChange;
  const bool heartbeatDue = (wakeCount % HEARTBEAT_EVERY_N_WAKES) == 0; // for check alive
  const bool firstBoot = !hasBaseline;

  const int zoneIdx = zoneForDistance(distanceMM);
  const bool zoneChanged = (zoneIdx != lastZoneIndex);
  if (zoneChanged && zoneIdx >= 0)
  {
    lastZoneIndex = zoneIdx;
    zoneNotificationPending = true;
  }
  const bool shouldNotify = suddenChange || zoneNotificationPending;

  Serial.printf(
      "Distance: %u mm (d=%d) | Light: %d (d=%d) | distSudden=%s lightSudden=%s heartbeat=%s zone=%s\r\n",
      distanceMM, distanceDelta, lightRaw, lightDelta,
      distanceSuddenChange ? "yes" : "no", lightSuddenChange ? "yes" : "no",
      heartbeatDue ? "yes" : "no",
      zoneIdx >= 0 ? zoneLabel[zoneIdx] : "n/a");

  if (shouldNotify || heartbeatDue || firstBoot)
  {
    if (connectWiFi() && connectMQTT())
    {
      mqttClient.subscribe(MQTT_TOPIC_THRESHOLD.c_str());

      // Push notification first
      if (shouldNotify && zoneIdx >= 0)
      {
        const bool discordSent = sendDiscordAlert(distanceMM, distanceDelta, zoneIdx, lightRaw, lightDelta);
        discordStatus = discordSent ? "sent" : "failed";
        if (discordSent)
        {
          zoneNotificationPending = false;
        }
      }

      // Updates lastDistanceMM/lastLightRaw, the baseline the active window compares against.
      publishSensor(distanceMM, lightRaw);

      // Loop replacement for control after deep sleep
      if (!firstBoot)
        runActiveWindow();
    }
    else
    {
      Serial.println("Network unavailable this cycle - will retry next wake.");
      lastDistanceMM = distanceMM;
      lastLightRaw = lightRaw;
      hasBaseline = true;
      // The notification never went out, retry next wake.
    }
  }
  else
  {
    lastDistanceMM = distanceMM;
    lastLightRaw = lightRaw;
    hasBaseline = true;
  }

  handlePowerButton();
  if (!deviceEnabled && !switchOffReported)
  {
    tryReportSwitchOff();
  }
  Serial.println("Idling - entering deep sleep.");
  digitalWrite(LED_PIN, LOW);
  delay(1000);
  digitalWrite(LED_PIN, HIGH);
  goToSleep(deviceEnabled ? CHECK_INTERVAL_US : SWITCH_OFF_INTERVAL_US);
}

void loop()
{
  // Chip back into deep sleep at the end, so loop() never gets reached, use setup() only.
}

// Publishes live sensor readings to MQTT.
void publishSensor(uint16_t distanceMM, int lightRaw)
{
  publishReadingsMQTT(distanceMM, lightRaw, deviceEnabled);

  lastDistanceMM = distanceMM;
  lastLightRaw = lightRaw;
  hasBaseline = true;
}

// Sends the final retained state while the network is still available.
// The RTC flag ensures an unchanged OFF switch does not report every timer wake.
void reportSwitchOff()
{
  publishReadingsMQTT(lastDistanceMM, lastLightRaw, false);
  switchOffReported = postFirebaseStatus("switch_off", false, distanceSensorStatus, mqttClient.connected(), discordStatus, FIREBASE_FINAL_RETRY_COUNT);
  if (switchOffReported)
    Serial.println("Final switch OFF state reported.");
  else
    Serial.println("Due to Firebase: Could not report switch OFF state - will retry next wake.");
}

void tryReportSwitchOff()
{
  if (mqttClient.connected() || (connectWiFi() && connectMQTT()))
  {
    reportSwitchOff();
  }
  else
  {
    Serial.println("Offline: Could not report switch OFF state - will retry next wake.");
  }
}

// Loop design to handle đeepsleep to conserve battery as much as possible.
void runActiveWindow()
{
  uint32_t lastFirebasePost = millis();
  uint32_t lastActivity = millis(); // reset on every sudden change; window ends when this goes stale

  while (millis() - lastActivity < awakeDurationMs)
  {
    if (handlePowerButton())
    {
      if (!switchOffReported)
      {
        reportSwitchOff();
      }
      return;
    }

    mqttClient.loop(); // process any incoming threshold/awake-duration updates

    if (settingsChanged)
    {
      Serial.println("Settings changed - blinking LED.");
      blinkLED(SETTINGS_BLINK_COUNT, SETTINGS_BLINK_MS);
      settingsChanged = false;
    }

    uint16_t distanceMM = lastDistanceMM;
    bool sensorOk = false;
    if (distanceSensorAvailable)
    {
      distanceMM = distanceSensor.read();
      sensorOk = !distanceSensor.timeoutOccurred();
      distanceSensorStatus = sensorOk ? "ok" : "timeout";
    }

    const int lightRaw = analogRead(LIGHT_PIN);
    const int distanceDelta = sensorOk ? abs((int)distanceMM - (int)lastDistanceMM) : 0;
    const int lightDelta = abs(lightRaw - lastLightRaw);
    const bool distanceSuddenChange = sensorOk && (distanceDelta > distanceThresholdMM);
    const bool lightSuddenChange = (lightDelta > lightThreshold);
    const bool suddenChange = distanceSuddenChange || lightSuddenChange;
    const int zoneIdx = sensorOk ? zoneForDistance(distanceMM) : -1;
    const bool zoneChanged = sensorOk && (zoneIdx != lastZoneIndex);
    if (zoneChanged)
    {
      lastZoneIndex = zoneIdx;
      zoneNotificationPending = true;
    }
    const bool shouldNotify = suddenChange || zoneNotificationPending;

    if (shouldNotify && zoneIdx >= 0)
    {
      Serial.printf("Zone notification during active window -> %s\r\n", zoneLabel[zoneIdx]);
      const bool discordSent = sendDiscordAlert(distanceMM, distanceDelta, zoneIdx, lightRaw, lightDelta);
      discordStatus = discordSent ? "sent" : "failed";
      if (discordSent)
      {
        zoneNotificationPending = false;
      }
    }

    if (zoneChanged)
    {
      lastActivity = millis();
    }

    if (suddenChange)
    {
      Serial.println("Additional sudden change during active window - resetting idle timer.");
      lastActivity = millis();
    }

    if (sensorOk)
    {
      publishSensor(distanceMM, lightRaw);
    }
    else
    {
      // Distance is unavailable, but light monitoring can continue.
      lastLightRaw = lightRaw;
      hasBaseline = true;
    }

    if (millis() - lastFirebasePost >= FIREBASE_STATUS_INTERVAL_MS)
    {
      lastFirebasePost = millis();
      postFirebaseStatus("active", true, distanceSensorStatus, mqttClient.connected(), discordStatus);
      if (!deviceEnabled)
      {
        Serial.println("Firebase attempt interrupted by button - stopping active window.");
        digitalWrite(LED_PIN, LOW);
        return;
      }
    }

    delay(ACTIVE_LOOP_DELAY_MS);
  }

  if (!postFirebaseStatus("idle", true, distanceSensorStatus, mqttClient.connected(), discordStatus, FIREBASE_FINAL_RETRY_COUNT))
  {
    Serial.println("Firebase: final idle status was not reported.");
  }
}

/*
 * XIAO ESP32-C3 + VL53L1X distance + TEMT6000 light sensor
 * -----------------------------------------------------------------
 * Behavior implemented:
 *   1. Sends both readings (distance + light) over MQTT to a broker ("server").
 *   2. Sends both readings to Firebase Realtime Database ("cloud").
 *   3. Deep-sleeps and only does the network work above when a "sudden
 *      change" in either reading is detected (or on a periodic heartbeat,
 *      see HEARTBEAT_EVERY_N_WAKES below, so it never goes fully silent).
 *   4. Sends a Discord webhook push whenever a sudden change (distance or
 *      light) or a zone crossing fires. The message is always the current
 *      distance zone's label plus the distance, with the light reading
 *      folded in too (e.g. "Close! (280 mm) | Light: 512 (change 220)") -
 *      there's no separate light-only message. The push is sent first,
 *      before the MQTT publish and the Firebase write, so it goes out with
 *      the least delay. See point 8 below for how zones work.
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
 * Required libraries (Library Manager):
 *   - VL53L1X by Pololu
 *   - PubSubClient by Nick O'Leary
 *   - ArduinoJson (v6.x) by Benoit Blanchon
 *   (WiFi, HTTPClient, WiFiClientSecure ship with the ESP32 Arduino core)
 *
 * Things you MUST fill in before this compiles into something useful:
 *   - FIREBASE_HOST / FIREBASE_AUTH  (Firebase Realtime Database)
 *   - DISCORD_WEBHOOK_URL            (Discord channel webhook)
 *
 * Hardware note: on the Seeed XIAO ESP32-C3, D6 (the switch pin) maps to
 * GPIO21, which is NOT one of the chip's RTC-capable GPIOs (only GPIO0-5
 * are). That means we can't hardware-wake the chip the instant the switch
 * flips during deep sleep - instead we just poll it on a timer, slower
 * while it's off (SWITCH_OFF_INTERVAL_US) than while it's on
 * (CHECK_INTERVAL_US).
 *
 * You can change the "sudden change" thresholds AND the awake duration
 * live, without reflashing, by publishing a retained MQTT message like
 * this to MQTT_TOPIC_THRESHOLD (any subset of the fields is fine):
 *   {"distance_mm":50,"light":100,"awake_ms":5000}
 * e.g. with mosquitto_pub:
 *   mosquitto_pub -h broker.hivemq.com -r -t xiao/esp32c3/sensors/threshold \
 *       -m '{"distance_mm":50,"light":100,"awake_ms":5000}'
 * The LED will blink 3 times to confirm the device picked up the change.
 *
 * 8. Distance zones with their own label (e.g. "Close!" under 30cm, "Far.."
 *    beyond that) are live configurable, on the same topic, by adding a
 *    "zones" array to the message above:
 *      {"zones":[{"max_mm":300,"label":"Close!"},
 *                {"max_mm":600,"label":"Medium"},
 *                {"max_mm":65535,"label":"Far.."}]}
 *    Zones must be listed in ascending order by max_mm - the last entry in
 *    the list is treated as the catch all for anything beyond it, so its
 *    max_mm value barely matters as long as it is the biggest one. The
 *    Discord push (point 4) always sends whichever zone label the current
 *    reading falls in, whether it was a sudden change or a plain zone
 *    crossing that triggered it. Up to MAX_ZONES zones are kept; sending
 *    fewer zones than that simply replaces the whole table with the
 *    shorter one.
 */

#include <Wire.h>
#include <VL53L1X.h>
#include <WiFi.h>

// PubSubClient defaults to a 256 byte MQTT packet limit. A "zones" update
// with several entries can exceed that, so raise it before including the
// header (must be defined before the #include, PubSubClient only applies it
// if nothing else set it first).
#define MQTT_MAX_PACKET_SIZE 512
#include <PubSubClient.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <esp_system.h>
#include <esp_sleep.h>

// ---------------- Pin configuration ----------------
constexpr int LIGHT_PIN  = D2;  // TEMT6000 OUT
constexpr int SWITCH_PIN = D6;  // Slide-switch center
constexpr int LED_PIN    = D10;

// ---------------- Wi-Fi ----------------
const char *WIFI_SSID     = "Wokwi-GUEST";
const char *WIFI_PASSWORD = "";

// ---------------- MQTT ("server") ----------------
const char *MQTT_BROKER          = "broker.hivemq.com";
constexpr uint16_t MQTT_PORT     = 1883;
const char *MQTT_TOPIC_DATA      = "xiao/esp32c3/sensors/data";
const char *MQTT_TOPIC_THRESHOLD = "xiao/esp32c3/sensors/threshold"; // publish retained JSON here to change thresholds

// ---------------- Firebase Realtime Database ("cloud") ----------------
// TODO: replace with your project's values.
const char *FIREBASE_HOST = "YOUR_PROJECT-default-rtdb.firebaseio.com"; // no "https://", no trailing slash
const char *FIREBASE_AUTH = "YOUR_DATABASE_SECRET_OR_ID_TOKEN";

// ---------------- Discord ----------------
// TODO: replace with your channel's webhook URL.
const char *DISCORD_WEBHOOK_URL = "https://discord.com/api/webhooks/1534801994722709614/JgQO7twiHuoDBFwq-3UtCqHw7Kk5jryMMk8xZRBBWznbow5eGuLxzE8xhc93ZnIbNDiF";

// ---------------- Timing ----------------
constexpr uint64_t CHECK_INTERVAL_US       = 2ULL * 1000000ULL;   // poll rate while switch is ON
constexpr uint64_t SWITCH_OFF_INTERVAL_US  = 5ULL * 1000000ULL;   // slower poll while switch is OFF
constexpr uint32_t HEARTBEAT_EVERY_N_WAKES = 30;                  // ~60s @ 2s interval: sync even without a sudden change
constexpr uint32_t WIFI_CONNECT_TIMEOUT_MS = 8000;
constexpr uint32_t MQTT_CONNECT_TIMEOUT_MS = 4000;
constexpr uint32_t ACTIVE_LOOP_DELAY_MS    = 250;                 // how often we re-check sensors during the awake window
constexpr uint8_t  SETTINGS_BLINK_COUNT    = 3;                   // LED blinks to confirm a threshold/awake setting change
constexpr uint16_t SETTINGS_BLINK_MS       = 100;                 // on/off duration per blink

// ---------------- Distance zones (live configurable over MQTT) ----------------
constexpr uint8_t  MAX_ZONES     = 5;   // cap on how many zones the "zones" MQTT field can set
constexpr uint8_t  ZONE_LABEL_LEN = 24; // max chars per zone label, including the null terminator

// ---------------- Persisted across deep-sleep cycles ----------------
// Deep sleep resets the chip, so ordinary globals get wiped - RTC_DATA_ATTR
// variables live in RTC memory and survive the reset.
RTC_DATA_ATTR uint16_t lastDistanceMM      = 0;
RTC_DATA_ATTR int      lastLightRaw        = 0;
RTC_DATA_ATTR bool     hasBaseline         = false;
RTC_DATA_ATTR uint16_t distanceThresholdMM = 100;  // default: >100mm swing = "sudden"
RTC_DATA_ATTR int      lightThreshold      = 200;  // default: >200 raw ADC counts = "sudden"
RTC_DATA_ATTR uint32_t awakeDurationMs     = 3000;  // idle timeout: stay awake this long after the LAST sudden change before sleeping
RTC_DATA_ATTR uint32_t wakeCount           = 0;

// Zone table: zoneMaxMM[i] / zoneLabel[i] pairs, ascending by max_mm, only the
// first zoneCount entries are valid. Default matches the classic "Close!" /
// "Far.." example: <=300mm is Close!, anything past that is Far..
RTC_DATA_ATTR uint16_t zoneMaxMM[MAX_ZONES]              = {300, 65535, 0, 0, 0};
RTC_DATA_ATTR char     zoneLabel[MAX_ZONES][ZONE_LABEL_LEN] = {"Close!", "Far..", "", "", ""};
RTC_DATA_ATTR uint8_t  zoneCount                         = 2;
RTC_DATA_ATTR int8_t   lastZoneIndex                     = -1; // -1 = unknown, forces a notification on first evaluation

VL53L1X distanceSensor;
WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

// Set by mqttCallback() whenever a threshold/awake setting actually changes;
// consumed (and cleared) in runActiveWindow() to trigger the confirmation blink.
// Plain global is fine - it only needs to live within a single wake cycle.
bool settingsChanged = false;

// ---------------- Forward declarations ----------------
bool connectWiFi();
bool connectMQTT();
void mqttCallback(char *topic, byte *payload, unsigned int length);
void publishReadingsMQTT(uint16_t distanceMM, int lightRaw);
void sendToFirebase(uint16_t distanceMM, int lightRaw, bool suddenChange);
void sendDiscordAlert(uint16_t distanceMM, uint8_t zoneIndex, int lightRaw, int lightDelta);
int8_t zoneForDistance(uint16_t distanceMM);
void goToSleep(uint64_t microseconds);
void blinkLED(uint8_t times, uint16_t intervalMs);
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

// ---------------- Wi-Fi / MQTT ----------------

bool connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  const uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_CONNECT_TIMEOUT_MS) {
    delay(100);
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("WiFi connected, IP: ");
    Serial.println(WiFi.localIP());
    return true;
  }

  Serial.println("WiFi connect failed/timeout.");
  return false;
}

bool connectMQTT() {
  mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
  mqttClient.setCallback(mqttCallback);
  mqttClient.setSocketTimeout(3);

  char clientId[32];
  snprintf(clientId, sizeof(clientId), "xiao-c3-%08lx", (unsigned long)esp_random());

  const uint32_t start = millis();
  while (!mqttClient.connected() && millis() - start < MQTT_CONNECT_TIMEOUT_MS) {
    if (mqttClient.connect(clientId)) {
      Serial.println("MQTT connected.");
      return true;
    }
    delay(200);
  }

  Serial.print("MQTT connect failed, state=");
  Serial.println(mqttClient.state());
  return false;
}

void mqttCallback(char *topic, byte *payload, unsigned int length) {
  if (strcmp(topic, MQTT_TOPIC_THRESHOLD) != 0) {
    return;
  }

  StaticJsonDocument<512> doc; // large enough for distance_mm/light/awake_ms plus a MAX_ZONES-entry "zones" array
  const DeserializationError err = deserializeJson(doc, payload, length);
  if (err) {
    Serial.print("Threshold message parse failed: ");
    Serial.println(err.c_str());
    return;
  }

  bool changed = false;

  if (doc.containsKey("distance_mm")) {
    const uint16_t v = doc["distance_mm"].as<uint16_t>();
    if (v != distanceThresholdMM) {
      distanceThresholdMM = v;
      changed = true;
    }
  }
  if (doc.containsKey("light")) {
    const int v = doc["light"].as<int>();
    if (v != lightThreshold) {
      lightThreshold = v;
      changed = true;
    }
  }
  if (doc.containsKey("awake_ms")) {
    const uint32_t v = doc["awake_ms"].as<uint32_t>();
    if (v != awakeDurationMs) {
      awakeDurationMs = v;
      changed = true;
    }
  }
  if (doc.containsKey("zones")) {
    JsonArray arr = doc["zones"].as<JsonArray>();
    uint8_t newCount = 0;
    for (JsonObject z : arr) {
      if (newCount >= MAX_ZONES) {
        Serial.println("Zones message has more entries than MAX_ZONES - extra ones ignored.");
        break;
      }
      if (!z.containsKey("max_mm") || !z.containsKey("label")) {
        continue; // skip malformed entries rather than aborting the whole update
      }
      zoneMaxMM[newCount] = z["max_mm"].as<uint16_t>();
      strlcpy(zoneLabel[newCount], z["label"].as<const char *>(), ZONE_LABEL_LEN);
      newCount++;
    }
    if (newCount > 0) {
      zoneCount = newCount;
      lastZoneIndex = -1; // re-evaluate against the new table, don't assume the old zone still lines up
      changed = true;
    } else {
      Serial.println("Zones message had no valid entries - table left unchanged.");
    }
  }

  if (changed) {
    settingsChanged = true; // consumed by runActiveWindow() to trigger the confirmation blink
    Serial.printf("Settings updated -> distance: %u mm, light: %d, awake: %lu ms\n",
        distanceThresholdMM, lightThreshold, (unsigned long)awakeDurationMs);
  }
}

void publishReadingsMQTT(uint16_t distanceMM, int lightRaw) {
  char payload[160];
  snprintf(payload, sizeof(payload),
      "{\"distance_mm\":%u,\"light\":%d,\"wake\":%lu}",
      distanceMM, lightRaw, (unsigned long)wakeCount);

  if (mqttClient.publish(MQTT_TOPIC_DATA, payload)) {
    Serial.print("MQTT published: ");
    Serial.println(payload);
  } else {
    Serial.println("MQTT publish failed.");
  }
}

// ---------------- Firebase ----------------

void sendToFirebase(uint16_t distanceMM, int lightRaw, bool suddenChange) {
  WiFiClientSecure client;
  client.setInsecure(); // prototype-friendly; pin Firebase's root CA for production use

  HTTPClient http;
  char url[192];
  snprintf(url, sizeof(url),
      "https://%s/readings/latest.json?auth=%s",
      FIREBASE_HOST, FIREBASE_AUTH);

  if (!http.begin(client, url)) {
    Serial.println("Firebase: begin() failed.");
    return;
  }

  http.addHeader("Content-Type", "application/json");

  char body[192];
  snprintf(body, sizeof(body),
      "{\"distance_mm\":%u,\"light\":%d,\"sudden_change\":%s,\"wake\":%lu}",
      distanceMM, lightRaw, suddenChange ? "true" : "false", (unsigned long)wakeCount);

  const int status = http.PUT(body);
  if (status > 0) {
    Serial.printf("Firebase PUT status: %d\n", status);
  } else {
    Serial.printf("Firebase PUT failed: %s\n", http.errorToString(status).c_str());
  }

  http.end();
}

// ---------------- Distance zones ----------------

// Returns the index of the zone the given distance falls into, or -1 if
// zoneCount is somehow 0. Zones are checked in ascending order; the last
// configured zone always matches anything that didn't match an earlier one,
// so it acts as the catch all regardless of its own max_mm value.
int8_t zoneForDistance(uint16_t distanceMM) {
  for (uint8_t i = 0; i < zoneCount; i++) {
    if (i == zoneCount - 1 || distanceMM <= zoneMaxMM[i]) {
      return (int8_t)i;
    }
  }
  return -1;
}

// Sends the configured label for the given zone, plus the current light
// reading, as a single Discord webhook message - this is the one and only
// push in the sketch now. It fires the same way whether a distance jump, a
// zone crossing, or a light jump is what triggered it; the light reading is
// always folded in rather than getting a separate message of its own. Swap
// this body out (e.g. for ntfy.sh or Pushover) if you want an actual phone
// push instead of a Discord message.
void sendDiscordAlert(uint16_t distanceMM, uint8_t zoneIndex, int lightRaw, int lightDelta) {
  WiFiClientSecure client;
  client.setInsecure(); // prototype-friendly; pin Discord's root CA for production use

  HTTPClient http;
  if (!http.begin(client, DISCORD_WEBHOOK_URL)) {
    Serial.println("Discord: begin() failed.");
    return;
  }

  http.addHeader("Content-Type", "application/json");

  char content[224];
  snprintf(content, sizeof(content),
      "{\"content\":\"%s (%u mm) | Light: %d (change %d)\"}",
      zoneLabel[zoneIndex], distanceMM, lightRaw, lightDelta);

  const int status = http.POST(content);
  if (status > 0) {
    Serial.printf("Discord webhook status: %d\n", status);
  } else {
    Serial.printf("Discord webhook failed: %s\n", http.errorToString(status).c_str());
  }

  http.end();
}

// ---------------- LED / active window ----------------

// Blinks the LED, then restores it to ON (this is only ever called while
// we already know the switch is on).
void blinkLED(uint8_t times, uint16_t intervalMs) {
  for (uint8_t i = 0; i < times; i++) {
    digitalWrite(LED_PIN, LOW);
    delay(intervalMs);
    digitalWrite(LED_PIN, HIGH);
    delay(intervalMs);
  }
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

// ---------------- Power management ----------------

void goToSleep(uint64_t microseconds) {
  mqttClient.disconnect();
  WiFi.disconnect(true);
  esp_sleep_enable_timer_wakeup(microseconds);
  Serial.flush();
  esp_deep_sleep_start();
}
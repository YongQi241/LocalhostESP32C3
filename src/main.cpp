// #include <Arduino.h>
// #include <WiFi.h>
// #include <PubSubClient.h>
// #include <Wire.h>
// #include <VL53L1X.h>
// #include <esp_system.h>

// VL53L1X sensorDis;

// constexpr uint8_t I2C_SDA_PIN = 6; // XIAO D4
// constexpr uint8_t I2C_SCL_PIN = 7; // XIAO D5

// const char *WIFI_SSID = "Wokwi-GUEST";
// const char *WIFI_PASSWORD = "";

// const char *MQTT_BROKER = "broker.hivemq.com";
// constexpr uint16_t MQTT_PORT = 1883;

// const char *MQTT_TOPIC =
//     "wokwi/esp32-c3/example-84721/data";

// constexpr uint32_t PUBLISH_INTERVAL_MS = 5000;
// constexpr uint32_t WIFI_RETRY_INTERVAL_MS = 5000;
// constexpr uint32_t MQTT_RETRY_INTERVAL_MS = 5000;

// WiFiClient wifiClient;
// PubSubClient mqttClient(wifiClient);

// String mqttClientId;

// uint32_t previousPublishTime = 0;
// uint32_t previousWiFiAttempt = 0;
// uint32_t previousMqttAttempt = 0;

// uint32_t messageNumber = 0;

// uint16_t distanceMm = 0;
// uint8_t rangeStatusCode = 255;

// bool hasReading = false;
// bool readingValid = false;
// bool mqttAttempted = false;

// /*
//  * Start Wi-Fi without waiting indefinitely.
//  */
// void startWiFi()
// {
//   Serial.print("Starting Wi-Fi connection to ");
//   Serial.println(WIFI_SSID);

//   WiFi.mode(WIFI_STA);
//   WiFi.begin(WIFI_SSID, WIFI_PASSWORD, 6);

//   previousWiFiAttempt = millis();
// }

// /*
//  * Retry Wi-Fi periodically without trapping loop().
//  */
// void maintainWiFi()
// {
//   static bool connectionReported = false;

//   if (WiFi.status() == WL_CONNECTED)
//   {
//     if (!connectionReported)
//     {
//       connectionReported = true;

//       Serial.println("Wi-Fi connected");
//       Serial.print("IP address: ");
//       Serial.println(WiFi.localIP());
//     }

//     return;
//   }

//   connectionReported = false;

//   const uint32_t now = millis();

//   if (now - previousWiFiAttempt < WIFI_RETRY_INTERVAL_MS)
//   {
//     return;
//   }

//   previousWiFiAttempt = now;

//   Serial.println("Retrying Wi-Fi connection...");

//   WiFi.disconnect();
//   WiFi.begin(WIFI_SSID, WIFI_PASSWORD, 6);
// }

// /*
//  * Attempt MQTT connection periodically.
//  *
//  * The random suffix helps prevent duplicate client IDs in Wokwi.
//  */
// void maintainMQTT()
// {
//   if (WiFi.status() != WL_CONNECTED)
//   {
//     return;
//   }

//   if (mqttClient.connected())
//   {
//     return;
//   }

//   const uint32_t now = millis();

//   if (mqttAttempted &&
//       now - previousMqttAttempt < MQTT_RETRY_INTERVAL_MS)
//   {
//     return;
//   }

//   mqttAttempted = true;
//   previousMqttAttempt = now;

//   Serial.print("Connecting to MQTT as ");
//   Serial.print(mqttClientId);
//   Serial.print("...");

//   if (mqttClient.connect(mqttClientId.c_str()))
//   {
//     Serial.println("connected");
//   }
//   else
//   {
//     Serial.print("failed, MQTT state=");
//     Serial.println(mqttClient.state());
//   }
// }

// /*
//  * Read only when the sensor says new data is available.
//  */
// void readDistanceSensor()
// {
//   if (!sensorDis.dataReady())
//   {
//     return;
//   }

//   const uint16_t newDistance = sensorDis.read(false);

//   if (sensorDis.timeoutOccurred())
//   {
//     readingValid = false;
//     Serial.println("VL53L1X read timeout");
//     return;
//   }

//   distanceMm = newDistance;
//   rangeStatusCode =
//       static_cast<uint8_t>(sensorDis.ranging_data.range_status);

//   hasReading = true;
//   readingValid =
//       sensorDis.ranging_data.range_status == VL53L1X::RangeValid;

//   Serial.print("Distance: ");
//   Serial.print(distanceMm);
//   Serial.print(" mm | ");

//   Serial.println(
//       VL53L1X::rangeStatusToString(
//           sensorDis.ranging_data.range_status));
// }

// void publishMessage()
// {
//   if (!mqttClient.connected())
//   {
//     Serial.println("Publish skipped: MQTT disconnected");
//     return;
//   }

//   if (!hasReading)
//   {
//     Serial.println("Publish skipped: no sensor reading");
//     return;
//   }

//   messageNumber++;

//   char payload[192];

//   const int payloadLength = snprintf(
//       payload,
//       sizeof(payload),
//       "{\"device\":\"esp32-c3\","
//       "\"message\":%lu,"
//       "\"uptime_ms\":%lu,"
//       "\"distance_mm\":%u,"
//       "\"valid\":%s,"
//       "\"range_status\":%u}",
//       static_cast<unsigned long>(messageNumber),
//       static_cast<unsigned long>(millis()),
//       distanceMm,
//       readingValid ? "true" : "false",
//       rangeStatusCode);

//   if (payloadLength < 0 ||
//       payloadLength >= static_cast<int>(sizeof(payload)))
//   {
//     Serial.println("MQTT payload formatting failed");
//     return;
//   }

//   if (mqttClient.publish(MQTT_TOPIC, payload))
//   {
//     Serial.print("Published to ");
//     Serial.print(MQTT_TOPIC);
//     Serial.print(": ");
//     Serial.println(payload);
//   }
//   else
//   {
//     Serial.print("MQTT publish failed, state=");
//     Serial.println(mqttClient.state());
//   }
// }

// void setupDistanceSensor()
// {
//   Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
//   Wire.setClock(400000);

//   sensorDis.setTimeout(500);

//   if (!sensorDis.init())
//   {
//     Serial.println("VL53L1X initialization failed");
//     Serial.println("Check wiring and library selection.");

//     while (true)
//     {
//       delay(1000);
//     }
//   }

//   sensorDis.setDistanceMode(VL53L1X::Long);
//   sensorDis.setMeasurementTimingBudget(50000);
//   sensorDis.startContinuous(50);

//   Serial.println("VL53L1X initialized");
// }

// void setup()
// {
//   Serial.begin(115200);
//   delay(500);

//   // Initialize the sensor independently of network availability.
//   setupDistanceSensor();

//   // Add a random suffix because Wokwi instances can share a MAC.
//   const uint64_t chipId = ESP.getEfuseMac();

//   char clientIdBuffer[64];

//   snprintf(
//       clientIdBuffer,
//       sizeof(clientIdBuffer),
//       "wokwi-esp32-c3-%08lx-%08lx",
//       static_cast<unsigned long>(chipId & 0xFFFFFFFF),
//       static_cast<unsigned long>(esp_random()));

//   mqttClientId = clientIdBuffer;

//   mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
//   mqttClient.setKeepAlive(30);
//   mqttClient.setSocketTimeout(3);

//   startWiFi();
// }

// void loop()
// {
//   readDistanceSensor();

//   maintainWiFi();
//   maintainMQTT();

//   if (mqttClient.connected())
//   {
//     // Required to process MQTT traffic and keepalive packets.
//     mqttClient.loop();
//   }

//   const uint32_t now = millis();

//   if (now - previousPublishTime >= PUBLISH_INTERVAL_MS)
//   {
//     previousPublishTime = now;
//     publishMessage();
//   }

//   delay(1);
// }


#include <Wire.h>
#include <VL53L1X.h>

constexpr int LIGHT_PIN  = D2;  // TEMT6000 OUT
constexpr int SWITCH_PIN = D6;  // Slide-switch center
constexpr int LED_PIN    = D10;

VL53L1X distanceSensor;

void setup() {
  Serial.begin(115200);

  pinMode(SWITCH_PIN, INPUT);
  pinMode(LED_PIN, OUTPUT);

  // VL53L1X: SDA=D4, SCL=D5
  Wire.begin(D4, D5);

  distanceSensor.setTimeout(500);

  if (!distanceSensor.init()) {
    Serial.println("VL53L1X not detected!");
    while (true) {
      digitalWrite(LED_PIN, !digitalRead(LED_PIN));
      delay(150);
    }
  }

  distanceSensor.setDistanceMode(VL53L1X::Long);
  distanceSensor.setMeasurementTimingBudget(50000);
  distanceSensor.startContinuous(50);

  Serial.println("System ready");
}

void loop() {
  int lightRaw = analogRead(LIGHT_PIN);
  bool switchOn = digitalRead(SWITCH_PIN);
  uint16_t distanceMM = distanceSensor.read();

  // LED follows the slide switch
  digitalWrite(LED_PIN, switchOn);

  Serial.printf(
    "Distance: %u mm | Light: %d | Switch: %s | LED: %s\n",
    distanceMM,
    lightRaw,
    switchOn ? "3V3" : "GND",
    switchOn ? "ON" : "OFF"
  );

  if (distanceSensor.timeoutOccurred()) {
    Serial.println("VL53L1X timeout!");
  }

  delay(250);
}
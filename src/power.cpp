#include "power.h"
#include "config.h"
#include "network.h"
#include <WiFi.h>
#include <esp_sleep.h>

void goToSleep(uint64_t microseconds)
{
  mqttClient.disconnect();
  WiFi.disconnect(true);
  esp_sleep_enable_timer_wakeup(microseconds);
  // SWITCH_PIN (D3 / GPIO5) is RTC-capable, so in addition to the timer
  // above, also wake the instant the switch reads high - removes the
  // polling lag that a non-RTC-capable pin would otherwise force.
  esp_deep_sleep_enable_gpio_wakeup(1ULL << GPIO_NUM_5, ESP_GPIO_WAKEUP_GPIO_HIGH);
  Serial.flush();
  esp_deep_sleep_start();
}

void blinkLED(uint8_t times, uint16_t intervalMs)
{
  for (uint8_t i = 0; i < times; i++)
  {
    digitalWrite(LED_PIN, LOW);
    delay(intervalMs);
    digitalWrite(LED_PIN, HIGH);
    delay(intervalMs);
  }
}
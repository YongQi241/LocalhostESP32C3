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
  if (digitalRead(SWITCH_PIN) == LOW) esp_deep_sleep_enable_gpio_wakeup(1ULL << GPIO_NUM_5, ESP_GPIO_WAKEUP_GPIO_HIGH);
  digitalWrite(LED_PIN, LOW);
  Serial.flush();
  esp_deep_sleep_start();
}

void blinkLED(int times, uint16_t intervalMs)
{
  for (int i = 0; i < times; i++)
  {
    digitalWrite(LED_PIN, LOW);
    delay(intervalMs);
    digitalWrite(LED_PIN, HIGH);
    delay(intervalMs);
  }
}


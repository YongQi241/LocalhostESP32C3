#include "power.h"
#include "config.h"
#include "network.h"
#include "state.h"
#include <WiFi.h>
#include <esp_sleep.h>


bool handlePowerButton()
{
  if (digitalRead(SWITCH_PIN) != LOW) return false;

  const uint32_t holdStarted = millis();
  while (millis() - holdStarted < BUTTON_HOLD_MS)
  {
    if (digitalRead(SWITCH_PIN) != LOW) return false;
    delay(10);
  }

  deviceEnabled = !deviceEnabled;
  digitalWrite(LED_PIN, deviceEnabled ? HIGH : LOW);
  Serial.printf("Button held for 2 seconds - device %s.\r\n", deviceEnabled ? "ON" : "OFF");

  // ONE ONLY, wait for button to release
  while (digitalRead(SWITCH_PIN) == LOW) delay(10);
  return true;
}

void goToSleep(uint64_t microseconds)
{
  mqttClient.disconnect();
  WiFi.disconnect(true);
  esp_sleep_enable_timer_wakeup(microseconds);
  // Do not enter level-triggered GPIO sleep while the button is still held.
  // Wait for button to release / insurance
  while (digitalRead(SWITCH_PIN) == LOW) delay(10);
  esp_deep_sleep_enable_gpio_wakeup(1ULL << GPIO_NUM_5, ESP_GPIO_WAKEUP_GPIO_LOW);
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

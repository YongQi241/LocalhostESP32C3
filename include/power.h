#ifndef POWER_H
#define POWER_H

#include <Arduino.h>

/* Disconnects MQTT/WiFi, turns off the LED, and puts the chip into deep sleep for the given duration. Never returns - the chip resets on wake and setup() runs again from the top. */
void goToSleep(uint64_t microseconds);

/* Blinks the LED, then restores it to ON (only ever called while we already know the switch is on). */
void blinkLED(int times, uint16_t intervalMs);

#endif // POWER_H

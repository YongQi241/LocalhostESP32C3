#ifndef FIREBASE_H
#define FIREBASE_H

#include <Arduino.h>

// Appends a timestamped device-status record to Firebase Realtime Database.
bool postFirebaseStatus(const char *state, bool switchOn, const char *sensorStatus, bool mqttConnected, const char *discordStatus, int maxAttempts = 1);

#endif // FIREBASE_H

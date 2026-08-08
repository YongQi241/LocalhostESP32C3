#ifndef FIREBASE_H
#define FIREBASE_H

#include <Arduino.h>

// Writes the given reading to Firebase Realtime Database, using
// FIREBASE_HOST/FIREBASE_AUTH from env_config.h.
void sendToFirebase(uint16_t distanceMM, int lightRaw, bool suddenChange);

#endif // FIREBASE_H

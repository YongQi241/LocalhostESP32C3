#ifndef FIREBASE_H
#define FIREBASE_H

#include <Arduino.h>

// Writes the given reading and switch state to Firebase Realtime Database.
void sendToFirebase(uint16_t distanceMM, int lightRaw, bool suddenChange, bool switchOn);

#endif // FIREBASE_H

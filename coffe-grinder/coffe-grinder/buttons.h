#pragma once
#include <Arduino.h>

struct Button {
  uint8_t pin;
  bool lastState;
  unsigned long lastDebounce;
};

inline bool btnPressed(Button &b) {
  bool reading = digitalRead(b.pin);
  if (reading == LOW && b.lastState == HIGH && millis() - b.lastDebounce > 50) {
    b.lastDebounce = millis();
    b.lastState = reading;
    return true;
  }
  b.lastState = reading;
  return false;
}

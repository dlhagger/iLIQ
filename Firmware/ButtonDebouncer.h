#pragma once

#include <Arduino.h>

class ButtonDebouncer {
 public:
  explicit ButtonDebouncer(uint8_t pin, uint16_t debounceMs = 35)
      : pin_(pin), debounceMs_(debounceMs) {}

  void begin() {
    pinMode(pin_, INPUT_PULLUP);
    stableState_ = digitalRead(pin_);
    lastReading_ = stableState_;
    lastChangeMs_ = millis();
    pressedEdge_ = false;
  }

  bool pollPressedEdge(unsigned long nowMs) {
    bool reading = digitalRead(pin_);
    if (reading != lastReading_) {
      lastReading_ = reading;
      lastChangeMs_ = nowMs;
    }

    if ((nowMs - lastChangeMs_) >= debounceMs_ && stableState_ != lastReading_) {
      stableState_ = lastReading_;
      if (stableState_ == LOW) {
        pressedEdge_ = true;
      }
    }

    if (pressedEdge_) {
      pressedEdge_ = false;
      return true;
    }

    return false;
  }

  bool isPressed() const { return stableState_ == LOW; }

 private:
  uint8_t pin_;
  uint16_t debounceMs_;
  bool stableState_ = HIGH;
  bool lastReading_ = HIGH;
  bool pressedEdge_ = false;
  unsigned long lastChangeMs_ = 0;
};

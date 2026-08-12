#pragma once

#include <Arduino.h>

enum class LedStatus { Boot, ApOnly, Connecting, Online, BlePairing, BleConnected, Busy, Error };

class LedService {
 public:
  struct Color {
    uint8_t red;
    uint8_t green;
    uint8_t blue;
  };

  void init(uint8_t pin);
  void setStatus(LedStatus status);
  void tick();

 private:
  void write(Color color);
  void writeOff();
  Color colorFor(LedStatus status) const;
  bool shouldBlink(LedStatus status) const;
  uint32_t blinkPeriodMs(LedStatus status) const;

  uint8_t pin_ = 21;
  LedStatus status_ = LedStatus::Boot;
  LedStatus lastRenderedStatus_ = LedStatus::Boot;
  bool lastRenderedOn_ = false;
  bool rendered_ = false;
  bool initialized_ = false;
};

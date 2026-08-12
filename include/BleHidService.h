#pragma once

#include <Arduino.h>

#include "AppConfig.h"

class BLECharacteristic;
class BLEHIDDevice;
class BLEServer;

class BleHidService {
 public:
  bool init(const DeviceConfig& config);
  BleHidState snapshot(bool includeBonds = false) const;
  bool removeBond(const String& address);
  bool clearBonds();

  bool typeText(const String& text);
  bool pressKey(uint8_t key);
  void releaseKeyboard();
  bool moveMouse(int8_t x, int8_t y, int8_t wheel = 0, int8_t pan = 0);
  bool pressMouse(uint8_t button);
  bool releaseMouse(uint8_t button);
  bool clickMouse(uint8_t button);
  void releaseMouseButtons();

  void setConnected(bool connected);
  void setPairing(bool pairing);

 private:
  bool sendKeyboardReport();
  bool sendMouseReport(int8_t x = 0, int8_t y = 0, int8_t wheel = 0, int8_t pan = 0);
  bool pressUsage(uint8_t usage, bool shift = false);
  uint8_t keyUsage(uint8_t key) const;
  uint8_t asciiUsage(char value, bool& shift) const;

  mutable SemaphoreHandle_t mutex_ = nullptr;
  BLEServer* server_ = nullptr;
  BLEHIDDevice* hid_ = nullptr;
  BLECharacteristic* input_ = nullptr;
  bool enabled_ = false;
  bool connected_ = false;
  bool pairing_ = false;
  bool keyboardMode_ = true;
  String name_;
  uint8_t modifiers_ = 0;
  uint8_t keys_[6] = {};
  uint8_t mouseButtons_ = 0;
};

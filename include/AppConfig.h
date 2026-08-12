#pragma once

#include <Arduino.h>
#include <vector>

constexpr uint8_t kDefaultGpioStopPin = 4;
constexpr const char* kGpioStopCommand = "STOP";

inline bool isGpioStopCommand(String value) {
  value.trim();
  value.toUpperCase();
  return value == kGpioStopCommand;
}

struct GpioBinding {
  uint8_t pin = 0;
  String profile;
  bool enabled = true;
};

struct DeviceConfig {
  String wifiSsid;
  String wifiPassword;
  String hostname = "hidpad-s3";
  String defaultProfile;
  String ttyPassword;
  String hidTransport = "usb";
  String bleMode = "keyboard";
};

struct BleHidState {
  bool enabled = false;
  bool connected = false;
  bool pairing = false;
  String mode = "keyboard";
  String name;
  std::vector<String> bonds;
};

struct WifiState {
  bool connected = false;
  bool connecting = false;
  bool apActive = false;
  bool passwordSet = false;
  bool ssidFound = false;
  uint8_t status = 0;
  int32_t rssi = 0;
  int32_t channel = 0;
  IPAddress apIp;
  IPAddress staIp;
  String statusText;
};

struct MacroStateView {
  bool busy = false;
  bool infinite = false;
  uint32_t repeatTarget = 1;
  uint32_t repeatDone = 0;
  String currentProfile;
  String message;
};

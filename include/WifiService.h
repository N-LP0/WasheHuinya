#pragma once

#include <Arduino.h>
#include <DNSServer.h>
#include <WiFi.h>

#include "AppConfig.h"

class WifiService {
 public:
  void init(const DeviceConfig& config);
  void applyConfig(const DeviceConfig& config);
  void tick();
  WifiState snapshot() const;
  uint32_t reconnectAttempts() const;

 private:
  String normalizedHostname(const DeviceConfig& config) const;
  void startAccessPoint();
  void stopAccessPoint();
  void startStationIfConfigured();

  DeviceConfig config_;
  DNSServer dnsServer_;
  mutable SemaphoreHandle_t mutex_ = nullptr;
  uint32_t lastStaAttemptMs_ = 0;
  uint32_t reconnectAttempts_ = 0;
  bool apActive_ = false;
  bool lastSsidFound_ = false;
  int32_t lastRssi_ = 0;
  int32_t lastChannel_ = 0;
};

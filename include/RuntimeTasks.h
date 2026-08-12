#pragma once

#include <Arduino.h>

#include "GpioService.h"
#include "BleHidService.h"
#include "LedService.h"
#include "MacroEngine.h"
#include "WebApiServer.h"
#include "WifiService.h"

class RuntimeTasks {
 public:
  void init(WifiService& wifi,
            BleHidService& ble,
            MacroEngine& macro,
            WebApiServer& web,
            GpioService& gpio,
            uint8_t ledPin);

 private:
  static void wifiTask(void* param);
  static void macroTask(void* param);
  static void webTask(void* param);
  static void gpioTask(void* param);
  static void ledTask(void* param);

  WifiService* wifi_ = nullptr;
  BleHidService* ble_ = nullptr;
  MacroEngine* macro_ = nullptr;
  WebApiServer* web_ = nullptr;
  GpioService* gpio_ = nullptr;
  LedService led_;

  LedStatus ledStatus_ = LedStatus::Boot;
  SemaphoreHandle_t mutex_ = nullptr;
};

#pragma once

#include <Arduino.h>

#include <functional>
#include <vector>

#include "AppConfig.h"
#include "CommandService.h"

class GpioService {
 public:
  using StartMacroCallback = std::function<bool(const String&)>;
  using FinishMacroCallback = std::function<bool()>;
  using StopMacroCallback = std::function<bool()>;

  void init(const std::vector<GpioBinding>& bindings);
  void setStartMacroCallback(StartMacroCallback cb);
  void setFinishMacroCallback(FinishMacroCallback cb);
  void setStopMacroCallback(StopMacroCallback cb);
  void setLogService(CommandService* cmd);
  void applyBindings(const std::vector<GpioBinding>& bindings);
  void tick();

  static bool isPinAllowed(uint8_t pin);

 private:
  struct InterruptSlot {
    GpioService* service = nullptr;
    uint8_t index = 0;
  };

  static constexpr size_t kMaxBindings = 12;

  static void IRAM_ATTR handleInterrupt(void* arg);
  void IRAM_ATTR markPendingFromIsr(uint8_t index);
  void detachAll();
  void setupBinding(size_t index, const GpioBinding& binding);

  GpioBinding bindings_[kMaxBindings];
  InterruptSlot slots_[kMaxBindings];
  volatile bool pending_[kMaxBindings] = {};
  volatile TickType_t lastChangeTick_[kMaxBindings] = {};
  bool stablePressed_[kMaxBindings] = {};
  size_t bindingCount_ = 0;
  int8_t activeIndex_ = -1;
  SemaphoreHandle_t mutex_ = nullptr;
  portMUX_TYPE isrMux_ = portMUX_INITIALIZER_UNLOCKED;
  StartMacroCallback startMacro_;
  FinishMacroCallback finishMacro_;
  StopMacroCallback stopMacro_;
  CommandService* cmd_ = nullptr;
};

#include "GpioService.h"

namespace {
constexpr TickType_t kDebounceTicks = pdMS_TO_TICKS(12);
}  // namespace

void GpioService::init(const std::vector<GpioBinding>& bindings) {
  mutex_ = xSemaphoreCreateMutex();
  applyBindings(bindings);
}

void GpioService::setStartMacroCallback(StartMacroCallback cb) {
  startMacro_ = cb;
}

void GpioService::setFinishMacroCallback(FinishMacroCallback cb) {
  finishMacro_ = cb;
}

void GpioService::setStopMacroCallback(StopMacroCallback cb) {
  stopMacro_ = cb;
}

void GpioService::setLogService(CommandService* cmd) {
  cmd_ = cmd;
}

void GpioService::applyBindings(const std::vector<GpioBinding>& bindings) {
  if (mutex_ == nullptr) {
    return;
  }

  xSemaphoreTake(mutex_, portMAX_DELAY);
  portENTER_CRITICAL(&isrMux_);
  interruptsEnabled_ = false;
  isrBindingCount_ = 0;
  portEXIT_CRITICAL(&isrMux_);
  detachAll();
  bindingCount_ = 0;
  activeIndex_ = -1;

  for (const GpioBinding& binding : bindings) {
    if (!binding.enabled || binding.profile.isEmpty() || !isPinAllowed(binding.pin)) {
      continue;
    }
    setupBinding(bindingCount_, binding);
    ++bindingCount_;
    if (bindingCount_ >= kMaxBindings) {
      break;
    }
  }

  for (size_t i = 0; i < bindingCount_; ++i) {
    attachInterruptArg(
        digitalPinToInterrupt(bindings_[i].pin),
        handleInterrupt,
        &slots_[i],
        CHANGE);
  }
  portENTER_CRITICAL(&isrMux_);
  isrBindingCount_ = bindingCount_;
  interruptsEnabled_ = true;
  portEXIT_CRITICAL(&isrMux_);
  xSemaphoreGive(mutex_);
}

void GpioService::tick() {
  if (mutex_ == nullptr) {
    return;
  }

  const TickType_t now = xTaskGetTickCount();
  for (size_t i = 0; i < kMaxBindings; ++i) {
    bool changed = false;
    GpioBinding binding;
    bool pressed = false;
    bool shouldStart = false;
    bool shouldFinish = false;
    bool shouldStop = false;

    xSemaphoreTake(mutex_, portMAX_DELAY);
    if (i < bindingCount_) {
      binding = bindings_[i];

      portENTER_CRITICAL(&isrMux_);
      const bool dirty = pending_[i];
      const TickType_t changedAt = lastChangeTick_[i];
      portEXIT_CRITICAL(&isrMux_);

      if (dirty && now - changedAt >= kDebounceTicks) {
        pressed = digitalRead(binding.pin) == LOW;
        if (pressed != stablePressed_[i]) {
          stablePressed_[i] = pressed;
          changed = true;
          if (pressed) {
            if (isGpioStopCommand(binding.profile)) {
              shouldStop = true;
            } else {
              activeIndex_ = static_cast<int8_t>(i);
              shouldStart = true;
            }
          } else if (activeIndex_ == static_cast<int8_t>(i)) {
            activeIndex_ = -1;
            shouldFinish = true;
          }
        }

        portENTER_CRITICAL(&isrMux_);
        if (lastChangeTick_[i] == changedAt) {
          pending_[i] = false;
        }
        portEXIT_CRITICAL(&isrMux_);
      }
    }
    xSemaphoreGive(mutex_);

    if (!changed) {
      continue;
    }

    if (shouldStop) {
      if (cmd_ != nullptr) {
        cmd_->appendLog(String("gpio stop: GPIO") + binding.pin);
      }
      if (stopMacro_ != nullptr) {
        stopMacro_();
      }
      continue;
    }

    if (shouldStart) {
      if (cmd_ != nullptr) {
        cmd_->appendLog(String("gpio press: GPIO") + binding.pin + " -> " + binding.profile);
      }

      if (startMacro_ != nullptr) {
        const bool ok = startMacro_(binding.profile);
        if (cmd_ != nullptr && !ok) {
          cmd_->appendLog(String("gpio start failed: ") + binding.profile);
        }
        if (!ok) {
          xSemaphoreTake(mutex_, portMAX_DELAY);
          activeIndex_ = -1;
          xSemaphoreGive(mutex_);
        }
      }
      continue;
    }

    if (shouldFinish) {
      if (cmd_ != nullptr) {
        cmd_->appendLog(String("gpio release: GPIO") + binding.pin);
      }
      if (finishMacro_ != nullptr) {
        finishMacro_();
      }
    }
  }
}

bool GpioService::isPinAllowed(uint8_t pin) {
  switch (pin) {
    case 4:
    case 5:
    case 6:
    case 7:
    case 8:
    case 9:
    case 10:
      return true;
    default:
      return false;
  }
}

void IRAM_ATTR GpioService::handleInterrupt(void* arg) {
  InterruptSlot* slot = static_cast<InterruptSlot*>(arg);
  if (slot == nullptr || slot->service == nullptr) {
    return;
  }
  slot->service->markPendingFromIsr(slot->index);
}

void IRAM_ATTR GpioService::markPendingFromIsr(uint8_t index) {
  const TickType_t now = xTaskGetTickCountFromISR();
  portENTER_CRITICAL_ISR(&isrMux_);
  if (interruptsEnabled_ && index < isrBindingCount_) {
    lastChangeTick_[index] = now;
    pending_[index] = true;
  }
  portEXIT_CRITICAL_ISR(&isrMux_);
}

void GpioService::detachAll() {
  for (size_t i = 0; i < bindingCount_; ++i) {
    detachInterrupt(digitalPinToInterrupt(bindings_[i].pin));
    portENTER_CRITICAL(&isrMux_);
    pending_[i] = false;
    lastChangeTick_[i] = 0;
    portEXIT_CRITICAL(&isrMux_);
    stablePressed_[i] = false;
  }
}

void GpioService::setupBinding(size_t index, const GpioBinding& binding) {
  bindings_[index] = binding;
  pinMode(binding.pin, INPUT_PULLUP);
  stablePressed_[index] = digitalRead(binding.pin) == LOW;
  portENTER_CRITICAL(&isrMux_);
  pending_[index] = false;
  lastChangeTick_[index] = 0;
  portEXIT_CRITICAL(&isrMux_);
  slots_[index].service = this;
  slots_[index].index = static_cast<uint8_t>(index);

}

#include "RuntimeTasks.h"

namespace {
RuntimeTasks* g_runtime = nullptr;

constexpr BaseType_t kNetworkCore = 0;
constexpr BaseType_t kHidCore = 1;

constexpr UBaseType_t kNetworkPriority = 2;
constexpr UBaseType_t kMacroPriority = 2;
constexpr UBaseType_t kLedPriority = 1;

constexpr uint32_t kWifiStackWords = 4096;
constexpr uint32_t kWebStackWords = 8192;
constexpr uint32_t kMacroStackWords = 6144;
constexpr uint32_t kGpioStackWords = 4096;
constexpr uint32_t kLedStackWords = 2048;

bool createTaskPinned(TaskFunction_t task,
                      const char* name,
                      uint32_t stackWords,
                      UBaseType_t priority,
                      BaseType_t core) {
  const BaseType_t result = xTaskCreatePinnedToCore(
      task,
      name,
      stackWords,
      nullptr,
      priority,
      nullptr,
      core);
  if (result == pdPASS) {
    Serial.printf("Task %s started on core %d\n", name, static_cast<int>(core));
    return true;
  }

  Serial.printf("Task %s failed on core %d\n", name, static_cast<int>(core));
  return false;
}
}  // namespace

void RuntimeTasks::init(WifiService& wifi,
                        BleHidService& ble,
                        MacroEngine& macro,
                        WebApiServer& web,
                        GpioService& gpio,
                        uint8_t ledPin) {
  wifi_ = &wifi;
  ble_ = &ble;
  macro_ = &macro;
  web_ = &web;
  gpio_ = &gpio;
  mutex_ = xSemaphoreCreateMutex();

  led_.init(ledPin);

  g_runtime = this;

  const bool wifiStarted =
      createTaskPinned(wifiTask, "wifiTask", kWifiStackWords, kNetworkPriority, kNetworkCore);
  const bool webStarted =
      createTaskPinned(webTask, "webTask", kWebStackWords, kNetworkPriority, kNetworkCore);
  const bool macroStarted =
      createTaskPinned(macroTask, "macroTask", kMacroStackWords, kMacroPriority, kHidCore);
  const bool gpioStarted =
      createTaskPinned(gpioTask, "gpioTask", kGpioStackWords, kMacroPriority, kHidCore);
  const bool ledStarted =
      createTaskPinned(ledTask, "ledTask", kLedStackWords, kLedPriority, kHidCore);

  if (!wifiStarted || !webStarted || !macroStarted || !gpioStarted || !ledStarted) {
    ledStatus_ = LedStatus::Error;
  }
}

void RuntimeTasks::wifiTask(void* param) {
  (void)param;
  for (;;) {
    g_runtime->wifi_->tick();

    const WifiState ws = g_runtime->wifi_->snapshot();
    const BleHidState ble = g_runtime->ble_->snapshot();
    const bool busy = g_runtime->macro_->isBusy();

    xSemaphoreTake(g_runtime->mutex_, portMAX_DELAY);
    if (busy) {
      g_runtime->ledStatus_ = LedStatus::Busy;
    } else if (ble.pairing) {
      g_runtime->ledStatus_ = LedStatus::BlePairing;
    } else if (ble.connected) {
      g_runtime->ledStatus_ = LedStatus::BleConnected;
    } else if (ws.connected) {
      g_runtime->ledStatus_ = LedStatus::Online;
    } else if (ws.connecting) {
      g_runtime->ledStatus_ = LedStatus::Connecting;
    } else {
      g_runtime->ledStatus_ = LedStatus::ApOnly;
    }
    xSemaphoreGive(g_runtime->mutex_);

    vTaskDelay(pdMS_TO_TICKS(25));
  }
}

void RuntimeTasks::macroTask(void* param) {
  (void)param;
  for (;;) {
    g_runtime->macro_->tick();
    vTaskDelay(pdMS_TO_TICKS(1));
  }
}

void RuntimeTasks::webTask(void* param) {
  (void)param;
  for (;;) {
    g_runtime->web_->handleClient();
    vTaskDelay(pdMS_TO_TICKS(30));
  }
}

void RuntimeTasks::gpioTask(void* param) {
  (void)param;
  for (;;) {
    g_runtime->gpio_->tick();
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}

void RuntimeTasks::ledTask(void* param) {
  (void)param;
  for (;;) {
    xSemaphoreTake(g_runtime->mutex_, portMAX_DELAY);
    const LedStatus status = g_runtime->ledStatus_;
    xSemaphoreGive(g_runtime->mutex_);

    g_runtime->led_.setStatus(status);
    g_runtime->led_.tick();
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

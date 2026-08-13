#include "Application.h"

#include <Arduino.h>
#include <USB.h>
#include <esp_coexist.h>

#include "CommandService.h"
#include "BleHidService.h"
#include "GpioService.h"
#include "MacroEngine.h"
#include "RuntimeTasks.h"
#include "StorageService.h"
#include "UpdateService.h"
#include "WebApiServer.h"
#include "WifiService.h"

namespace {

constexpr uint8_t kStatusRgbPin = 21;
constexpr uint32_t kCpuFrequencyMhz = 160;

StorageService g_storage;
WifiService g_wifi;
MacroEngine g_macro;
BleHidService g_ble;
CommandService g_cmd;
GpioService g_gpio;
UpdateService g_update;
WebApiServer g_web(g_storage, g_wifi, g_macro, g_ble, g_cmd, g_gpio, g_update);
RuntimeTasks g_tasks;
bool g_servicesReady = false;
bool g_webReady = false;

void initUsb() {
  USB.productName("HID MacroPad S3");
  USB.manufacturerName("Copilot");
  USB.serialNumber("0003");
  USB.webUSB(false);
  USB.begin();
}

void logHeap(const char* stage) {
  Serial.printf("Heap after %s: free=%u largest=%u\n",
                stage,
                ESP.getFreeHeap(),
                ESP.getMaxAllocHeap());
}

}  // namespace

namespace Application {

bool init() {
  Serial.begin(115200);
  delay(100);
  Serial.println("Boot");
  setCpuFrequencyMhz(kCpuFrequencyMhz);
  Serial.printf("CPU frequency: %u MHz\n", getCpuFrequencyMhz());

  initUsb();

  if (!g_storage.init()) {
    Serial.println("Storage init failed");
    return false;
  }
  g_update.init(g_storage);
  Serial.println(g_storage.filesystemReady() ? "Storage ready" : "Storage filesystem unavailable");
  logHeap("storage");

  DeviceConfig cfg = g_storage.loadConfig();
  if (!g_ble.init(cfg)) {
    Serial.println("BLE HID init failed");
    return false;
  }
  if (cfg.hidTransport == "ble") {
    const esp_err_t coexistResult = esp_coex_preference_set(ESP_COEX_PREFER_BALANCE);
    Serial.printf("WiFi/BLE coexistence: %s\n", esp_err_to_name(coexistResult));
  }
  g_macro.init(&g_ble, cfg.hidTransport);
  logHeap(cfg.hidTransport == "ble" ? "BLE HID" : "USB HID");

  // Start Wi-Fi after the selected HID transport so ESP32 coexistence is already configured.
  g_wifi.init(cfg);
  logHeap("Wi-Fi");
  if (!g_cmd.init()) {
    Serial.println("Command service init failed");
    return false;
  }

  g_cmd.setExecuteMacroCallback([&](const String& profile) {
    const String script = g_storage.loadProfile(profile);
    if (script.isEmpty()) {
      return false;
    }
    return g_macro.start(profile, script);
  });

  g_cmd.setExecuteLineCallback([&](const String& line) { return g_macro.runCommandLine(line); });
  g_cmd.setStopMacroCallback([&]() { return g_macro.stop(); });
  g_gpio.setLogService(&g_cmd);
  g_gpio.setStartMacroCallback([&](const String& profile) {
    const String script = g_storage.loadProfile(profile);
    if (script.isEmpty()) {
      return false;
    }
    g_macro.stop();
    return g_macro.start(profile, script);
  });
  g_gpio.setFinishMacroCallback([&]() { return g_macro.finishAfterCurrentRun(); });
  g_gpio.setStopMacroCallback([&]() { return g_macro.stop(); });

  g_gpio.init(g_storage.loadGpioBindings());

  g_web.init();
  g_webReady = true;

  g_tasks.init(g_wifi, g_ble, g_macro, g_web, g_gpio, kStatusRgbPin);
  logHeap("runtime tasks");
  g_servicesReady = true;
  Serial.println("Runtime tasks started");
  return true;
}

void loop() {
  vTaskDelay(pdMS_TO_TICKS(1000));
}

}  // namespace Application

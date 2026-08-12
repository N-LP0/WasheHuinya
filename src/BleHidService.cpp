#include "BleHidService.h"

#include <BLE2902.h>
#include <BLEAdvertising.h>
#include <BLEDevice.h>
#include <BLEHIDDevice.h>
#include <BLESecurity.h>
#include <USBHIDKeyboard.h>
#include <USBHIDMouse.h>
#include <esp_gap_ble_api.h>

namespace {
constexpr uint16_t kEspressifBluetoothCompanyId = 0x02E5;
constexpr uint16_t kKeyboardProductId = 0x0001;
constexpr uint16_t kMouseProductId = 0x0002;

constexpr uint8_t kKeyboardReportMap[] = {
    0x05, 0x01, 0x09, 0x06, 0xA1, 0x01, 0x85, 0x01, 0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7,
    0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x08, 0x81, 0x02, 0x95, 0x01, 0x75, 0x08,
    0x81, 0x01, 0x95, 0x05, 0x75, 0x01, 0x05, 0x08, 0x19, 0x01, 0x29, 0x05, 0x91, 0x02,
    0x95, 0x01, 0x75, 0x03, 0x91, 0x01, 0x95, 0x06, 0x75, 0x08, 0x15, 0x00, 0x25, 0x65,
    0x05, 0x07, 0x19, 0x00, 0x29, 0x65, 0x81, 0x00, 0xC0};

constexpr uint8_t kMouseReportMap[] = {
    0x05, 0x01, 0x09, 0x02, 0xA1, 0x01, 0x85, 0x01, 0x09, 0x01, 0xA1, 0x00, 0x05, 0x09,
    0x19, 0x01, 0x29, 0x03, 0x15, 0x00, 0x25, 0x01, 0x95, 0x03, 0x75, 0x01, 0x81, 0x02,
    0x95, 0x01, 0x75, 0x05, 0x81, 0x01, 0x05, 0x01, 0x09, 0x30, 0x09, 0x31, 0x09, 0x38,
    0x15, 0x81, 0x25, 0x7F, 0x75, 0x08, 0x95, 0x03, 0x81, 0x06, 0x05, 0x0C, 0x0A, 0x38,
    0x02, 0x15, 0x81, 0x25, 0x7F, 0x75, 0x08, 0x95, 0x01, 0x81, 0x06, 0xC0, 0xC0};

class ServerCallbacks final : public BLEServerCallbacks {
 public:
  explicit ServerCallbacks(BleHidService& owner) : owner_(owner) {}
  void onConnect(BLEServer*) override { owner_.setConnected(true); }
  void onDisconnect(BLEServer* server) override {
    owner_.setConnected(false);
    owner_.setPairing(false);
    server->startAdvertising();
  }

 private:
  BleHidService& owner_;
};

class SecurityCallbacks final : public BLESecurityCallbacks {
 public:
  explicit SecurityCallbacks(BleHidService& owner) : owner_(owner) {}

  uint32_t onPassKeyRequest() override {
    owner_.setPairing(true);
    return 0;
  }
  void onPassKeyNotify(uint32_t) override { owner_.setPairing(true); }
  bool onSecurityRequest() override {
    owner_.setPairing(true);
    return true;
  }
  void onAuthenticationComplete(esp_ble_auth_cmpl_t) override { owner_.setPairing(false); }
  bool onConfirmPIN(uint32_t) override {
    owner_.setPairing(true);
    return true;
  }

 private:
  BleHidService& owner_;
};

String formatAddress(const esp_bd_addr_t address) {
  char text[18];
  snprintf(text,
           sizeof(text),
           "%02X:%02X:%02X:%02X:%02X:%02X",
           address[0],
           address[1],
           address[2],
           address[3],
           address[4],
           address[5]);
  return String(text);
}

bool parseAddress(const String& text, esp_bd_addr_t address) {
  unsigned int bytes[6];
  if (sscanf(text.c_str(),
             "%02x:%02x:%02x:%02x:%02x:%02x",
             &bytes[0],
             &bytes[1],
             &bytes[2],
             &bytes[3],
             &bytes[4],
             &bytes[5]) != 6) {
    return false;
  }
  for (size_t i = 0; i < 6; ++i) address[i] = static_cast<uint8_t>(bytes[i]);
  return true;
}

uint8_t mouseMask(uint8_t button) {
  if (button == MOUSE_LEFT) return 0x01;
  if (button == MOUSE_RIGHT) return 0x02;
  if (button == MOUSE_MIDDLE) return 0x04;
  return 0;
}
}  // namespace

bool BleHidService::init(const DeviceConfig& config) {
  mutex_ = xSemaphoreCreateMutex();
  enabled_ = config.hidTransport == "ble";
  keyboardMode_ = config.bleMode != "mouse";
  name_ = keyboardMode_ ? "HIDPad S3 keyboard" : "HIDPad S3 mouse";
  if (!enabled_ || mutex_ == nullptr) return mutex_ != nullptr;

  BLEDevice::init(name_.c_str());
  server_ = BLEDevice::createServer();
  server_->setCallbacks(new ServerCallbacks(*this));
  hid_ = new BLEHIDDevice(server_);
  input_ = hid_->inputReport(1);
  if (keyboardMode_) hid_->outputReport(1);
  hid_->manufacturer()->setValue("HIDPad");
  BLECharacteristic* model = hid_->deviceInfo()->createCharacteristic(
      static_cast<uint16_t>(0x2A24), BLECharacteristic::PROPERTY_READ);
  model->setValue(name_.c_str());
  hid_->pnp(0x01,
            kEspressifBluetoothCompanyId,
            keyboardMode_ ? kKeyboardProductId : kMouseProductId,
            0x0100);
  hid_->hidInfo(0x00, 0x01);
  hid_->reportMap(const_cast<uint8_t*>(keyboardMode_ ? kKeyboardReportMap : kMouseReportMap),
                  keyboardMode_ ? sizeof(kKeyboardReportMap) : sizeof(kMouseReportMap));
  hid_->startServices();
  hid_->setBatteryLevel(100);

  BLESecurity* security = new BLESecurity();
  security->setAuthenticationMode(ESP_LE_AUTH_BOND);
  security->setCapability(ESP_IO_CAP_NONE);
  BLEDevice::setSecurityCallbacks(new SecurityCallbacks(*this));

  BLEAdvertising* advertising = server_->getAdvertising();
  advertising->setAppearance(keyboardMode_ ? HID_KEYBOARD : HID_MOUSE);
  advertising->addServiceUUID(hid_->hidService()->getUUID());
  BLEAdvertisementData scanResponse;
  scanResponse.setName(name_.c_str());
  advertising->setScanResponseData(scanResponse);
  advertising->start();
  return true;
}

BleHidState BleHidService::snapshot(bool includeBonds) const {
  BleHidState state;
  if (mutex_) xSemaphoreTake(mutex_, portMAX_DELAY);
  state.enabled = enabled_;
  state.connected = connected_;
  state.pairing = pairing_;
  state.mode = keyboardMode_ ? "keyboard" : "mouse";
  state.name = name_;
  if (mutex_) xSemaphoreGive(mutex_);

  if (state.enabled && includeBonds) {
    int count = esp_ble_get_bond_device_num();
    if (count > 0) {
      std::vector<esp_ble_bond_dev_t> devices(static_cast<size_t>(count));
      if (esp_ble_get_bond_device_list(&count, devices.data()) == ESP_OK) {
        for (int i = 0; i < count; ++i) state.bonds.push_back(formatAddress(devices[i].bd_addr));
      }
    }
  }
  return state;
}

bool BleHidService::removeBond(const String& addressText) {
  if (!enabled_) return false;
  esp_bd_addr_t address;
  if (!parseAddress(addressText, address)) return false;
  return esp_ble_remove_bond_device(address) == ESP_OK;
}

bool BleHidService::clearBonds() {
  if (!enabled_) return false;
  int count = esp_ble_get_bond_device_num();
  if (count < 0) return false;
  if (count == 0) return true;

  std::vector<esp_ble_bond_dev_t> devices(static_cast<size_t>(count));
  if (esp_ble_get_bond_device_list(&count, devices.data()) != ESP_OK) return false;
  bool ok = true;
  for (int i = 0; i < count; ++i) {
    if (esp_ble_remove_bond_device(devices[i].bd_addr) != ESP_OK) ok = false;
  }
  return ok;
}

void BleHidService::setConnected(bool connected) {
  if (!mutex_) return;
  xSemaphoreTake(mutex_, portMAX_DELAY);
  connected_ = connected;
  xSemaphoreGive(mutex_);
}

void BleHidService::setPairing(bool pairing) {
  if (!mutex_) return;
  xSemaphoreTake(mutex_, portMAX_DELAY);
  pairing_ = pairing;
  xSemaphoreGive(mutex_);
}

bool BleHidService::typeText(const String& text) {
  if (!enabled_ || !keyboardMode_ || !connected_) return false;
  for (size_t i = 0; i < text.length(); ++i) {
    bool shift = false;
    const uint8_t usage = asciiUsage(text[i], shift);
    if (usage == 0 || !pressUsage(usage, shift)) return false;
    delay(2);
    releaseKeyboard();
  }
  return true;
}

bool BleHidService::pressKey(uint8_t key) {
  if (!enabled_ || !keyboardMode_ || !connected_) return false;
  if (key >= KEY_LEFT_CTRL && key <= KEY_RIGHT_GUI) {
    modifiers_ |= static_cast<uint8_t>(1U << (key - KEY_LEFT_CTRL));
    return sendKeyboardReport();
  }
  if (key < 0x80) {
    bool shift = false;
    const uint8_t usage = asciiUsage(static_cast<char>(key), shift);
    return usage != 0 && pressUsage(usage, shift);
  }
  const uint8_t usage = keyUsage(key);
  return usage != 0 && pressUsage(usage);
}

bool BleHidService::pressUsage(uint8_t usage, bool shift) {
  if (shift) modifiers_ |= 0x02;
  for (uint8_t key : keys_) if (key == usage) return sendKeyboardReport();
  for (uint8_t& key : keys_) {
    if (key == 0) {
      key = usage;
      return sendKeyboardReport();
    }
  }
  return false;
}

void BleHidService::releaseKeyboard() {
  modifiers_ = 0;
  memset(keys_, 0, sizeof(keys_));
  sendKeyboardReport();
}

bool BleHidService::sendKeyboardReport() {
  if (!connected_ || !input_) return false;
  uint8_t report[8] = {modifiers_, 0, keys_[0], keys_[1], keys_[2], keys_[3], keys_[4], keys_[5]};
  input_->setValue(report, sizeof(report));
  input_->notify();
  return true;
}

bool BleHidService::moveMouse(int8_t x, int8_t y, int8_t wheel, int8_t pan) {
  return sendMouseReport(x, y, wheel, pan);
}

bool BleHidService::pressMouse(uint8_t button) {
  const uint8_t mask = mouseMask(button);
  if (!mask) return false;
  mouseButtons_ |= mask;
  return sendMouseReport();
}

bool BleHidService::releaseMouse(uint8_t button) {
  const uint8_t mask = mouseMask(button);
  if (!mask) return false;
  mouseButtons_ &= static_cast<uint8_t>(~mask);
  return sendMouseReport();
}

bool BleHidService::clickMouse(uint8_t button) {
  if (!pressMouse(button)) return false;
  delay(8);
  return releaseMouse(button);
}

void BleHidService::releaseMouseButtons() {
  mouseButtons_ = 0;
  sendMouseReport();
}

bool BleHidService::sendMouseReport(int8_t x, int8_t y, int8_t wheel, int8_t pan) {
  if (!enabled_ || keyboardMode_ || !connected_ || !input_) return false;
  uint8_t report[5] = {mouseButtons_, static_cast<uint8_t>(x), static_cast<uint8_t>(y),
                       static_cast<uint8_t>(wheel), static_cast<uint8_t>(pan)};
  input_->setValue(report, sizeof(report));
  input_->notify();
  return true;
}

uint8_t BleHidService::keyUsage(uint8_t key) const {
  if (key >= KEY_F1 && key <= KEY_F12) return static_cast<uint8_t>(0x3A + key - KEY_F1);
  switch (key) {
    case KEY_RETURN: return 0x28;
    case KEY_ESC: return 0x29;
    case KEY_BACKSPACE: return 0x2A;
    case KEY_TAB: return 0x2B;
    case KEY_INSERT: return 0x49;
    case KEY_HOME: return 0x4A;
    case KEY_PAGE_UP: return 0x4B;
    case KEY_DELETE: return 0x4C;
    case KEY_END: return 0x4D;
    case KEY_PAGE_DOWN: return 0x4E;
    case KEY_RIGHT_ARROW: return 0x4F;
    case KEY_LEFT_ARROW: return 0x50;
    case KEY_DOWN_ARROW: return 0x51;
    case KEY_UP_ARROW: return 0x52;
    default: return 0;
  }
}

uint8_t BleHidService::asciiUsage(char value, bool& shift) const {
  shift = false;
  if (value >= 'a' && value <= 'z') return static_cast<uint8_t>(0x04 + value - 'a');
  if (value >= 'A' && value <= 'Z') { shift = true; return static_cast<uint8_t>(0x04 + value - 'A'); }
  if (value >= '1' && value <= '9') return static_cast<uint8_t>(0x1E + value - '1');
  if (value == '0') return 0x27;
  if (value == ' ') return 0x2C;
  const char plain[] = "-=[]\\;',./`";
  const uint8_t usages[] = {0x2D,0x2E,0x2F,0x30,0x31,0x33,0x34,0x36,0x37,0x38,0x35};
  const char shifted[] = "_+{}|:\"<>?~";
  for (size_t i = 0; i < sizeof(usages); ++i) {
    if (value == plain[i]) return usages[i];
    if (value == shifted[i]) { shift = true; return usages[i]; }
  }
  const char numberShift[] = "!@#$%^&*()";
  for (size_t i = 0; i < 10; ++i) if (value == numberShift[i]) {
    shift = true;
    return i == 9 ? 0x27 : static_cast<uint8_t>(0x1E + i);
  }
  if (value == '\n' || value == '\r') return 0x28;
  if (value == '\t') return 0x2B;
  return 0;
}

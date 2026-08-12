#include "StorageService.h"

#include <ArduinoJson.h>

namespace {
constexpr char kPrefsNs[] = "hidpad";
constexpr char kProfilesDir[] = "/profiles";
constexpr char kLittleFsPartition[] = "littlefs";
constexpr char kGpioBindingsKey[] = "gpio";
constexpr size_t kMaxGpioBindings = 12;

bool isStoredGpioPinAllowed(uint8_t pin) {
  return pin >= 4 && pin <= 10;
}
}  // namespace

bool StorageService::init() {
  mutex_ = xSemaphoreCreateMutex();
  if (mutex_ == nullptr) {
    return false;
  }
  if (!LittleFS.begin(false, "/littlefs", 10, kLittleFsPartition)) {
    fsReady_ = false;
    return true;
  }
  fsReady_ = true;
  ensureProfilesDir();
  return true;
}

bool StorageService::filesystemReady() const {
  return fsReady_;
}

DeviceConfig StorageService::loadConfig() {
  DeviceConfig config;
  Preferences prefs;

  xSemaphoreTake(mutex_, portMAX_DELAY);
  prefs.begin(kPrefsNs, true);
  config.wifiSsid = prefs.getString("ssid", "");
  config.wifiPassword = prefs.getString("pass", "");
  config.hostname = prefs.getString("host", "hidpad-s3");
  config.defaultProfile = prefs.getString("profile", "");
  config.ttyPassword = prefs.getString("ttyPass", "");
  config.hidTransport = prefs.getString("hidOut", "usb");
  config.bleMode = prefs.getString("bleMode", "keyboard");
  prefs.end();
  xSemaphoreGive(mutex_);

  if (!isValidName(config.hostname)) {
    config.hostname = "hidpad-s3";
  }
  if (!config.defaultProfile.isEmpty() && !isValidName(config.defaultProfile)) {
    config.defaultProfile = "";
  }
  if (config.hidTransport != "ble") config.hidTransport = "usb";
  if (config.bleMode != "mouse") config.bleMode = "keyboard";
  return config;
}

bool StorageService::saveConfig(const DeviceConfig& config) {
  if (!isValidName(config.hostname) ||
      (!config.defaultProfile.isEmpty() && !isValidName(config.defaultProfile))) {
    return false;
  }
  Preferences prefs;
  xSemaphoreTake(mutex_, portMAX_DELAY);
  const bool ok = prefs.begin(kPrefsNs, false);
  if (!ok) {
    xSemaphoreGive(mutex_);
    return false;
  }
  prefs.putString("ssid", config.wifiSsid);
  prefs.putString("pass", config.wifiPassword);
  prefs.putString("host", config.hostname);
  prefs.putString("profile", config.defaultProfile);
  prefs.putString("ttyPass", config.ttyPassword);
  prefs.putString("hidOut", config.hidTransport);
  prefs.putString("bleMode", config.bleMode);
  prefs.end();
  xSemaphoreGive(mutex_);
  return true;
}

std::vector<GpioBinding> StorageService::loadGpioBindings() {
  std::vector<GpioBinding> bindings;
  Preferences prefs;

  xSemaphoreTake(mutex_, portMAX_DELAY);
  prefs.begin(kPrefsNs, true);
  const String json = prefs.getString(kGpioBindingsKey, "[]");
  prefs.end();
  xSemaphoreGive(mutex_);

  JsonDocument doc;
  if (deserializeJson(doc, json) != DeserializationError::Ok || !doc.is<JsonArray>()) {
    return normalizeGpioBindings(bindings);
  }

  JsonArray arr = doc.as<JsonArray>();
  for (JsonObject item : arr) {
    GpioBinding binding;
    binding.pin = item["pin"] | 0;
    const String rawProfile = String(item["profile"] | "");
    if (isGpioStopCommand(rawProfile)) {
      binding.profile = kGpioStopCommand;
    } else if (isValidName(rawProfile)) {
      binding.profile = rawProfile;
    } else {
      continue;
    }
    binding.enabled = item["enabled"] | true;
    bindings.push_back(binding);
    if (bindings.size() >= kMaxGpioBindings) {
      break;
    }
  }

  return normalizeGpioBindings(bindings);
}

bool StorageService::saveGpioBindings(const std::vector<GpioBinding>& bindings) {
  const std::vector<GpioBinding> normalized = normalizeGpioBindings(bindings);
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  size_t count = 0;
  for (const GpioBinding& binding : normalized) {
    if (binding.profile.isEmpty()) {
      continue;
    }
    JsonObject item = arr.add<JsonObject>();
    item["pin"] = binding.pin;
    item["profile"] = isGpioStopCommand(binding.profile) ? String(kGpioStopCommand)
                                                         : binding.profile;
    item["enabled"] = binding.enabled;
    ++count;
    if (count >= kMaxGpioBindings) {
      break;
    }
  }

  String json;
  serializeJson(doc, json);

  Preferences prefs;
  xSemaphoreTake(mutex_, portMAX_DELAY);
  const bool ok = prefs.begin(kPrefsNs, false);
  if (!ok) {
    xSemaphoreGive(mutex_);
    return false;
  }
  prefs.putString(kGpioBindingsKey, json);
  prefs.end();
  xSemaphoreGive(mutex_);
  return true;
}

std::vector<GpioBinding> StorageService::normalizeGpioBindings(const std::vector<GpioBinding>& bindings) {
  GpioBinding stopBinding;
  stopBinding.pin = kDefaultGpioStopPin;
  stopBinding.profile = kGpioStopCommand;
  stopBinding.enabled = true;

  bool stopFound = false;
  for (const GpioBinding& binding : bindings) {
    if (!isGpioStopCommand(binding.profile)) {
      continue;
    }
    stopBinding.pin = binding.pin;
    stopBinding.profile = kGpioStopCommand;
    stopBinding.enabled = true;
    stopFound = true;
    break;
  }

  if (stopBinding.pin == 0) {
    stopBinding.pin = kDefaultGpioStopPin;
  }
  if (!isStoredGpioPinAllowed(stopBinding.pin)) {
    stopBinding.pin = kDefaultGpioStopPin;
  }

  std::vector<GpioBinding> normalized;
  normalized.reserve(bindings.size() + (stopFound ? 0 : 1));
  normalized.push_back(stopBinding);

  for (const GpioBinding& binding : bindings) {
    if (isGpioStopCommand(binding.profile) || binding.pin == stopBinding.pin) {
      continue;
    }
    GpioBinding clean = binding;
    if (!isValidName(clean.profile) || !isStoredGpioPinAllowed(clean.pin)) {
      continue;
    }

    bool duplicatePin = false;
    for (const GpioBinding& existing : normalized) {
      if (existing.pin == clean.pin) {
        duplicatePin = true;
        break;
      }
    }
    if (duplicatePin) {
      continue;
    }

    normalized.push_back(clean);
    if (normalized.size() >= kMaxGpioBindings) {
      break;
    }
  }

  return normalized;
}

bool StorageService::saveProfile(const String& name, const String& script) {
  if (!isValidName(name)) {
    return false;
  }
  if (!fsReady_) {
    return false;
  }

  xSemaphoreTake(mutex_, portMAX_DELAY);
  const bool dirOk = LittleFS.exists(kProfilesDir) || LittleFS.mkdir(kProfilesDir);
  if (!dirOk) {
    xSemaphoreGive(mutex_);
    return false;
  }

  File file = LittleFS.open(profilePath(name), FILE_WRITE);
  if (!file) {
    xSemaphoreGive(mutex_);
    return false;
  }
  const size_t written = file.print(script);
  file.close();
  xSemaphoreGive(mutex_);
  return written == script.length();
}

String StorageService::loadProfile(const String& name) {
  if (!isValidName(name)) {
    return "";
  }
  if (!fsReady_) {
    return "";
  }

  xSemaphoreTake(mutex_, portMAX_DELAY);
  File file = LittleFS.open(profilePath(name), FILE_READ);
  if (!file) {
    xSemaphoreGive(mutex_);
    return "";
  }
  const String script = file.readString();
  file.close();
  xSemaphoreGive(mutex_);
  return script;
}

bool StorageService::deleteProfile(const String& name) {
  if (!isValidName(name)) {
    return false;
  }
  if (!fsReady_) {
    return false;
  }

  xSemaphoreTake(mutex_, portMAX_DELAY);
  const bool ok = LittleFS.remove(profilePath(name));
  xSemaphoreGive(mutex_);
  return ok;
}

bool StorageService::profileExists(const String& name) {
  if (!isValidName(name)) {
    return false;
  }
  if (!fsReady_) {
    return false;
  }

  xSemaphoreTake(mutex_, portMAX_DELAY);
  const bool ok = LittleFS.exists(profilePath(name));
  xSemaphoreGive(mutex_);
  return ok;
}

std::vector<String> StorageService::listProfiles() {
  std::vector<String> names;
  if (!fsReady_) {
    return names;
  }

  xSemaphoreTake(mutex_, portMAX_DELAY);
  File root = LittleFS.open(kProfilesDir);
  if (root && root.isDirectory()) {
    File file = root.openNextFile();
    while (file) {
      String n = file.name();
      file.close();
      const int slash = n.lastIndexOf('/');
      if (slash >= 0) {
        n.remove(0, slash + 1);
      }
      if (n.endsWith(".txt")) {
        n.remove(n.length() - 4);
        if (isValidName(n)) {
          names.push_back(n);
        }
      }
      file = root.openNextFile();
    }
  }
  xSemaphoreGive(mutex_);

  return names;
}

bool StorageService::isValidName(const String& name) {
  if (name.isEmpty()) {
    return false;
  }
  for (size_t i = 0; i < name.length(); ++i) {
    const char c = name[i];
    if (!isalnum(static_cast<unsigned char>(c)) && c != '_' && c != '-') {
      return false;
    }
  }
  return true;
}

bool StorageService::ensureProfilesDir() {
  if (!fsReady_) {
    return false;
  }
  xSemaphoreTake(mutex_, portMAX_DELAY);
  const bool exists = LittleFS.exists(kProfilesDir);
  const bool ok = exists || LittleFS.mkdir(kProfilesDir);
  xSemaphoreGive(mutex_);
  return ok;
}

String StorageService::profilePath(const String& name) const {
  return String(kProfilesDir) + "/" + name + ".txt";
}

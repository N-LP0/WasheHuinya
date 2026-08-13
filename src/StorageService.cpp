#include "StorageService.h"

#include "AppLimits.h"

#include <ArduinoJson.h>
#include <algorithm>

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
  if (!mutex_) return false;
  xSemaphoreTake(mutex_, portMAX_DELAY);
  const bool ready = fsReady_;
  xSemaphoreGive(mutex_);
  return ready;
}

bool StorageService::setFilesystemMounted(bool mounted) {
  if (!mutex_) return false;
  xSemaphoreTake(mutex_, portMAX_DELAY);
  if (mounted) {
    fsReady_ = LittleFS.begin(false, "/littlefs", 10, kLittleFsPartition);
    lastError_ = fsReady_ ? "" : "LittleFS remount failed";
  } else {
    LittleFS.end();
    fsReady_ = false;
    lastError_ = "";
  }
  const bool ok = mounted ? fsReady_ : true;
  xSemaphoreGive(mutex_);
  return ok;
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
    lastError_ = "NVS config namespace could not be opened";
    xSemaphoreGive(mutex_);
    return false;
  }
  bool written = true;
  written &= prefs.putString("ssid", config.wifiSsid) == config.wifiSsid.length();
  written &= prefs.putString("pass", config.wifiPassword) == config.wifiPassword.length();
  written &= prefs.putString("host", config.hostname) == config.hostname.length();
  written &= prefs.putString("profile", config.defaultProfile) == config.defaultProfile.length();
  written &= prefs.putString("ttyPass", config.ttyPassword) == config.ttyPassword.length();
  written &= prefs.putString("hidOut", config.hidTransport) == config.hidTransport.length();
  written &= prefs.putString("bleMode", config.bleMode) == config.bleMode.length();
  const bool verified =
      prefs.getString("ssid", "") == config.wifiSsid &&
      prefs.getString("pass", "") == config.wifiPassword &&
      prefs.getString("host", "") == config.hostname &&
      prefs.getString("profile", "") == config.defaultProfile &&
      prefs.getString("ttyPass", "") == config.ttyPassword &&
      prefs.getString("hidOut", "") == config.hidTransport &&
      prefs.getString("bleMode", "") == config.bleMode;
  prefs.end();
  lastError_ = written && verified ? "" : "NVS config write verification failed";
  xSemaphoreGive(mutex_);
  return written && verified;
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
    lastError_ = "NVS GPIO namespace could not be opened";
    xSemaphoreGive(mutex_);
    return false;
  }
  const bool written = prefs.putString(kGpioBindingsKey, json) == json.length();
  const bool verified = prefs.getString(kGpioBindingsKey, "") == json;
  prefs.end();
  lastError_ = written && verified ? "" : "NVS GPIO write verification failed";
  xSemaphoreGive(mutex_);
  return written && verified;
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
  if (!isValidName(name) || script.length() > AppLimits::kMaxScriptBytes) {
    return false;
  }
  xSemaphoreTake(mutex_, portMAX_DELAY);
  if (!fsReady_) {
    lastError_ = "LittleFS is not mounted";
    xSemaphoreGive(mutex_);
    return false;
  }
  const String path = profilePath(name);
  size_t existingSize = 0;
  File existing = LittleFS.open(path, FILE_READ);
  if (existing) {
    existingSize = existing.size();
    existing.close();
  }
  const size_t growth = script.length() > existingSize ? script.length() - existingSize : 0;
  const size_t totalBytes = LittleFS.totalBytes();
  const size_t usedBytes = LittleFS.usedBytes();
  const size_t freeBytes = totalBytes > usedBytes ? totalBytes - usedBytes : 0;
  if (freeBytes < growth + AppLimits::kFilesystemReserveBytes) {
    lastError_ = "Not enough LittleFS space";
    xSemaphoreGive(mutex_);
    return false;
  }
  const bool dirOk = LittleFS.exists(kProfilesDir) || LittleFS.mkdir(kProfilesDir);
  if (!dirOk) {
    lastError_ = "Profiles directory could not be created";
    xSemaphoreGive(mutex_);
    return false;
  }

  File file = LittleFS.open(path, FILE_WRITE);
  if (!file) {
    lastError_ = "Profile file could not be opened for writing";
    xSemaphoreGive(mutex_);
    return false;
  }
  const size_t written = file.print(script);
  file.close();
  lastError_ = written == script.length() ? "" : "Profile write was incomplete";
  xSemaphoreGive(mutex_);
  return written == script.length();
}

String StorageService::loadProfile(const String& name) {
  if (!isValidName(name)) {
    return "";
  }
  xSemaphoreTake(mutex_, portMAX_DELAY);
  if (!fsReady_) {
    xSemaphoreGive(mutex_);
    return "";
  }
  File file = LittleFS.open(profilePath(name), FILE_READ);
  if (!file) {
    xSemaphoreGive(mutex_);
    return "";
  }
  if (file.size() > AppLimits::kMaxScriptBytes) {
    file.close();
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
  xSemaphoreTake(mutex_, portMAX_DELAY);
  if (!fsReady_) {
    xSemaphoreGive(mutex_);
    return false;
  }
  const bool ok = LittleFS.remove(profilePath(name));
  xSemaphoreGive(mutex_);
  return ok;
}

bool StorageService::profileExists(const String& name) {
  if (!isValidName(name)) {
    return false;
  }
  xSemaphoreTake(mutex_, portMAX_DELAY);
  if (!fsReady_) {
    xSemaphoreGive(mutex_);
    return false;
  }
  const bool ok = LittleFS.exists(profilePath(name));
  xSemaphoreGive(mutex_);
  return ok;
}

std::vector<String> StorageService::listProfiles() {
  std::vector<String> names;
  xSemaphoreTake(mutex_, portMAX_DELAY);
  if (!fsReady_) {
    xSemaphoreGive(mutex_);
    return names;
  }
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

StorageService::Result StorageService::importProfilesAtomic(
    const std::vector<std::pair<String, String>>& profiles) {
  if (profiles.empty() || profiles.size() > AppLimits::kMaxImportProfiles) return Result::Invalid;

  std::vector<String> names;
  names.reserve(profiles.size());
  size_t stagedBytes = 0;
  for (const auto& item : profiles) {
    if (!isValidName(item.first)) return Result::Invalid;
    if (item.second.length() > AppLimits::kMaxScriptBytes) return Result::TooLarge;
    if (std::find(names.begin(), names.end(), item.first) != names.end()) return Result::Invalid;
    names.push_back(item.first);
    stagedBytes += item.second.length();
  }
  if (!filesystemReady()) return Result::IoError;

  xSemaphoreTake(mutex_, portMAX_DELAY);
  auto tempPath = [](const String& name) { return String(kProfilesDir) + "/." + name + ".import.tmp"; };
  auto backupPath = [](const String& name) { return String(kProfilesDir) + "/." + name + ".import.bak"; };
  auto cleanup = [&](bool removeBackups) {
    for (const auto& item : profiles) {
      LittleFS.remove(tempPath(item.first));
      if (removeBackups) LittleFS.remove(backupPath(item.first));
    }
  };

  for (const auto& item : profiles) {
    const String target = profilePath(item.first);
    const String backup = backupPath(item.first);
    LittleFS.remove(tempPath(item.first));
    if (LittleFS.exists(backup)) {
      if (LittleFS.exists(target)) {
        LittleFS.remove(backup);
      } else if (!LittleFS.rename(backup, target)) {
        lastError_ = "Failed to recover interrupted profile import";
        xSemaphoreGive(mutex_);
        return Result::IoError;
      }
    }
  }

  const size_t totalBytes = LittleFS.totalBytes();
  const size_t usedBytes = LittleFS.usedBytes();
  const size_t freeBytes = totalBytes > usedBytes ? totalBytes - usedBytes : 0;
  if (freeBytes < stagedBytes + AppLimits::kFilesystemReserveBytes) {
    lastError_ = "Not enough LittleFS space for atomic import";
    xSemaphoreGive(mutex_);
    return Result::NoSpace;
  }

  for (size_t i = 0; i < profiles.size(); ++i) {
    File staged = LittleFS.open(tempPath(profiles[i].first), FILE_WRITE);
    if (!staged || staged.print(profiles[i].second) != profiles[i].second.length()) {
      if (staged) staged.close();
      cleanup(false);
      lastError_ = "Failed to stage imported profiles";
      xSemaphoreGive(mutex_);
      return Result::IoError;
    }
    staged.close();
  }

  size_t committed = 0;
  bool ok = true;
  for (size_t i = 0; i < profiles.size(); ++i) {
    const String target = profilePath(profiles[i].first);
    const String backup = backupPath(profiles[i].first);
    if (LittleFS.exists(target) && !LittleFS.rename(target, backup)) {
      ok = false;
      break;
    }
    if (!LittleFS.rename(tempPath(profiles[i].first), target)) {
      if (LittleFS.exists(backup)) LittleFS.rename(backup, target);
      ok = false;
      break;
    }
    ++committed;
  }

  if (ok) {
    for (size_t i = 0; i < profiles.size(); ++i) {
      File file = LittleFS.open(profilePath(profiles[i].first), FILE_READ);
      if (!file || file.size() != profiles[i].second.length()) ok = false;
      if (file) file.close();
      if (!ok) break;
    }
  }

  if (!ok) {
    for (size_t i = 0; i < committed; ++i) LittleFS.remove(profilePath(profiles[i].first));
    for (size_t i = 0; i < profiles.size(); ++i) {
      const String backup = backupPath(profiles[i].first);
      if (LittleFS.exists(backup)) LittleFS.rename(backup, profilePath(profiles[i].first));
    }
    cleanup(false);
    lastError_ = "Atomic profile import failed and was rolled back";
    xSemaphoreGive(mutex_);
    return Result::IoError;
  }

  cleanup(true);
  lastError_ = "";
  xSemaphoreGive(mutex_);
  return Result::Ok;
}

String StorageService::lastError() const {
  if (!mutex_) return "Storage is not initialized";
  xSemaphoreTake(mutex_, portMAX_DELAY);
  const String error = lastError_;
  xSemaphoreGive(mutex_);
  return error;
}

bool StorageService::isValidName(const String& name) {
  if (name.isEmpty() || name.length() > AppLimits::kMaxNameLength) {
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
  xSemaphoreTake(mutex_, portMAX_DELAY);
  if (!fsReady_) {
    xSemaphoreGive(mutex_);
    return false;
  }
  const bool exists = LittleFS.exists(kProfilesDir);
  const bool ok = exists || LittleFS.mkdir(kProfilesDir);
  xSemaphoreGive(mutex_);
  return ok;
}

String StorageService::profilePath(const String& name) const {
  return String(kProfilesDir) + "/" + name + ".txt";
}

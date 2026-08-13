#pragma once

#include <Arduino.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <utility>
#include <vector>

#include "AppConfig.h"

class StorageService {
 public:
  enum class Result { Ok, Invalid, TooLarge, NoSpace, IoError };

  bool init();
  bool filesystemReady() const;
  bool setFilesystemMounted(bool mounted);

  DeviceConfig loadConfig();
  bool saveConfig(const DeviceConfig& config);
  std::vector<GpioBinding> loadGpioBindings();
  bool saveGpioBindings(const std::vector<GpioBinding>& bindings);

  bool saveProfile(const String& name, const String& script);
  String loadProfile(const String& name);
  bool deleteProfile(const String& name);
  bool profileExists(const String& name);
  std::vector<String> listProfiles();
  Result importProfilesAtomic(const std::vector<std::pair<String, String>>& profiles);
  String lastError() const;

  static bool isValidName(const String& name);
  static std::vector<GpioBinding> normalizeGpioBindings(const std::vector<GpioBinding>& bindings);

 private:
  bool ensureProfilesDir();
  String profilePath(const String& name) const;

  SemaphoreHandle_t mutex_ = nullptr;
  bool fsReady_ = false;
  String lastError_;
};

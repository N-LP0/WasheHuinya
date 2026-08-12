#pragma once

#include <Arduino.h>

class UpdateService {
 public:
  enum class UpdateType {
    Firmware,
    Filesystem,
  };

  struct UpdateProgress {
    bool inProgress = false;
    bool completed = false;
    UpdateType type = UpdateType::Firmware;
    size_t totalSize = 0;
    size_t writtenSize = 0;
    String errorMessage;
  };

  bool begin(UpdateType type, size_t totalSize);
  bool write(const uint8_t* data, size_t length);
  bool end();
  void abort(const String& reason);
  UpdateProgress progress() const;

 private:
  UpdateProgress progress_;
};

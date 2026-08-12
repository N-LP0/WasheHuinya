#include "UpdateService.h"

#include <LittleFS.h>
#include <Update.h>

bool UpdateService::begin(UpdateType type, size_t totalSize) {
  if (progress_.inProgress) {
    progress_.errorMessage = "Update already in progress";
    return false;
  }
  if (totalSize == 0) {
    progress_.errorMessage = "Update file is empty";
    return false;
  }

  progress_ = UpdateProgress{};
  progress_.inProgress = true;
  progress_.type = type;
  progress_.totalSize = totalSize;

  if (type == UpdateType::Filesystem) {
    LittleFS.end();
  }

  const int command = type == UpdateType::Firmware ? U_FLASH : U_SPIFFS;
  if (!Update.begin(totalSize, command)) {
    progress_.inProgress = false;
    progress_.errorMessage = Update.errorString();
    return false;
  }

  return true;
}

bool UpdateService::write(const uint8_t* data, size_t length) {
  if (!progress_.inProgress) {
    progress_.errorMessage = "No update in progress";
    return false;
  }

  const size_t written = Update.write(const_cast<uint8_t*>(data), length);
  if (written != length) {
    progress_.inProgress = false;
    progress_.errorMessage = Update.errorString();
    return false;
  }

  progress_.writtenSize += written;
  return true;
}

bool UpdateService::end() {
  if (!progress_.inProgress) {
    progress_.errorMessage = "No update in progress";
    return false;
  }

  if (!Update.end(true)) {
    progress_.inProgress = false;
    progress_.errorMessage = Update.errorString();
    return false;
  }

  progress_.inProgress = false;
  progress_.completed = true;
  return true;
}

void UpdateService::abort(const String& reason) {
  if (progress_.inProgress) {
    Update.abort();
  }
  progress_.inProgress = false;
  progress_.completed = false;
  progress_.errorMessage = reason;
}

UpdateService::UpdateProgress UpdateService::progress() const {
  return progress_;
}

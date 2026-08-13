#pragma once

#include <Arduino.h>
#include <mbedtls/sha256.h>

class StorageService;

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
    String stage = "idle";
    String sha256;
    String errorMessage;
  };

  void init(StorageService& storage);
  bool begin(UpdateType type, size_t totalSize, const String& expectedSha256);
  bool write(const uint8_t* data, size_t length);
  bool end();
  void abort(const String& reason);
  UpdateProgress progress() const;

 private:
  bool restoreFilesystem();
  String finishSha256();

  StorageService* storage_ = nullptr;
  UpdateProgress progress_;
  mbedtls_sha256_context sha256Context_;
  String expectedSha256_;
  bool sha256Active_ = false;
  bool filesystemUnmounted_ = false;
};

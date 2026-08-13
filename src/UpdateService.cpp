#include "UpdateService.h"

#include "StorageService.h"

#include <LittleFS.h>
#include <Update.h>

namespace {
bool isSha256(const String& value) {
  if (value.length() != 64) return false;
  for (size_t i = 0; i < value.length(); ++i) {
    if (!isxdigit(static_cast<unsigned char>(value[i]))) return false;
  }
  return true;
}
}  // namespace

void UpdateService::init(StorageService& storage) {
  storage_ = &storage;
}

bool UpdateService::begin(UpdateType type, size_t totalSize, const String& expectedSha256) {
  if (progress_.inProgress) {
    progress_.errorMessage = "Update already in progress";
    return false;
  }
  if (totalSize == 0) {
    progress_.errorMessage = "Update file is empty";
    return false;
  }
  if (!isSha256(expectedSha256)) {
    progress_.errorMessage = "X-Update-SHA256 must contain 64 hexadecimal characters";
    return false;
  }

  progress_ = UpdateProgress{};
  progress_.inProgress = true;
  progress_.type = type;
  progress_.totalSize = totalSize;
  progress_.stage = "preparing";
  expectedSha256_ = expectedSha256;
  expectedSha256_.toLowerCase();

  if (type == UpdateType::Filesystem) {
    if (!storage_ || !storage_->setFilesystemMounted(false)) {
      progress_.inProgress = false;
      progress_.errorMessage = "LittleFS unmount failed";
      return false;
    }
    filesystemUnmounted_ = true;
  }

  const int command = type == UpdateType::Firmware ? U_FLASH : U_SPIFFS;
  if (!Update.begin(totalSize, command)) {
    progress_.inProgress = false;
    progress_.errorMessage = Update.errorString();
    restoreFilesystem();
    return false;
  }

  mbedtls_sha256_init(&sha256Context_);
  if (mbedtls_sha256_starts_ret(&sha256Context_, 0) != 0) {
    Update.abort();
    progress_.inProgress = false;
    progress_.errorMessage = "SHA-256 initialization failed";
    restoreFilesystem();
    return false;
  }
  sha256Active_ = true;
  progress_.stage = "uploading";
  return true;
}

bool UpdateService::write(const uint8_t* data, size_t length) {
  if (!progress_.inProgress) {
    progress_.errorMessage = "No update in progress";
    return false;
  }

  const size_t written = Update.write(const_cast<uint8_t*>(data), length);
  if (written != length) {
    const String error = Update.errorString();
    abort(error);
    return false;
  }

  if (!sha256Active_ || mbedtls_sha256_update_ret(&sha256Context_, data, length) != 0) {
    abort("SHA-256 calculation failed");
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

  progress_.stage = "verifying";
  if (progress_.writtenSize != progress_.totalSize) {
    Update.abort();
    progress_.inProgress = false;
    progress_.errorMessage = "Uploaded size does not match X-Update-Size";
    if (sha256Active_) {
      mbedtls_sha256_free(&sha256Context_);
      sha256Active_ = false;
    }
    restoreFilesystem();
    return false;
  }

  const String actualSha256 = finishSha256();
  progress_.sha256 = actualSha256;
  if (actualSha256.isEmpty() || actualSha256 != expectedSha256_) {
    Update.abort();
    progress_.inProgress = false;
    progress_.errorMessage = "Uploaded file SHA-256 does not match X-Update-SHA256";
    restoreFilesystem();
    return false;
  }

  progress_.stage = "committing";
  if (!Update.end(false)) {
    progress_.inProgress = false;
    progress_.errorMessage = Update.errorString();
    restoreFilesystem();
    return false;
  }

  progress_.inProgress = false;
  progress_.completed = true;
  progress_.stage = "completed";
  return true;
}

void UpdateService::abort(const String& reason) {
  if (Update.isRunning()) {
    Update.abort();
  }
  progress_.inProgress = false;
  progress_.completed = false;
  progress_.stage = "failed";
  progress_.errorMessage = reason;
  if (sha256Active_) {
    mbedtls_sha256_free(&sha256Context_);
    sha256Active_ = false;
  }
  restoreFilesystem();
}

UpdateService::UpdateProgress UpdateService::progress() const {
  return progress_;
}

bool UpdateService::restoreFilesystem() {
  if (!filesystemUnmounted_) return true;
  filesystemUnmounted_ = false;
  if (storage_ && storage_->setFilesystemMounted(true)) return true;
  if (!progress_.errorMessage.isEmpty()) progress_.errorMessage += "; ";
  progress_.errorMessage += "LittleFS remount failed";
  return false;
}

String UpdateService::finishSha256() {
  if (!sha256Active_) return "";
  uint8_t digest[32];
  const int result = mbedtls_sha256_finish_ret(&sha256Context_, digest);
  mbedtls_sha256_free(&sha256Context_);
  sha256Active_ = false;
  if (result != 0) return "";

  char hex[65];
  for (size_t i = 0; i < sizeof(digest); ++i) {
    snprintf(hex + i * 2, 3, "%02x", digest[i]);
  }
  hex[64] = '\0';
  return String(hex);
}

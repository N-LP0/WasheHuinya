#pragma once

#include <Arduino.h>

namespace AppLimits {
constexpr size_t kMaxNameLength = 64;
constexpr size_t kMaxScriptBytes = 32U * 1024U;
constexpr size_t kMaxImportBytes = 256U * 1024U;
constexpr size_t kMaxImportProfiles = 64;
constexpr size_t kFilesystemReserveBytes = 4096;

constexpr int kMaxDelayMs = 86400000;
constexpr int kMaxRepeatCount = 100000000;
constexpr int kMinMouseMove = -32767;
constexpr int kMaxMouseMove = 32767;
constexpr int kMinMouseScroll = -10000;
constexpr int kMaxMouseScroll = 10000;
constexpr size_t kMacroInstructionsPerTick = 64;
}  // namespace AppLimits

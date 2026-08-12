#include "LedService.h"

namespace {
constexpr LedService::Color kOff{0, 0, 0};
constexpr LedService::Color kBoot{2, 2, 2};
constexpr LedService::Color kApOnly{4, 2, 0};
constexpr LedService::Color kConnecting{0, 0, 4};
constexpr LedService::Color kOnline{0, 4, 0};
constexpr LedService::Color kBlePairing{0, 3, 4};
constexpr LedService::Color kBleConnected{0, 4, 4};
constexpr LedService::Color kBusy{3, 0, 3};
constexpr LedService::Color kError{4, 0, 0};
}  // namespace

void LedService::init(uint8_t pin) {
  pin_ = pin;
  initialized_ = true;
  setStatus(LedStatus::Boot);
  tick();
}

void LedService::setStatus(LedStatus status) {
  status_ = status;
}

void LedService::tick() {
  if (!initialized_) {
    return;
  }

  const bool on = !shouldBlink(status_) || ((millis() / blinkPeriodMs(status_)) % 2) == 0;
  if (rendered_ && status_ == lastRenderedStatus_ && on == lastRenderedOn_) {
    return;
  }

  rendered_ = true;
  lastRenderedStatus_ = status_;
  lastRenderedOn_ = on;
  if (on) {
    write(colorFor(status_));
    return;
  }
  writeOff();
}

void LedService::write(Color color) {
  neopixelWrite(pin_, color.red, color.green, color.blue);
}

void LedService::writeOff() {
  write(kOff);
}

LedService::Color LedService::colorFor(LedStatus status) const {
  switch (status) {
    case LedStatus::Boot:
      return kBoot;
    case LedStatus::ApOnly:
      return kApOnly;
    case LedStatus::Connecting:
      return kConnecting;
    case LedStatus::Online:
      return kOnline;
    case LedStatus::BlePairing:
      return kBlePairing;
    case LedStatus::BleConnected:
      return kBleConnected;
    case LedStatus::Busy:
      return kBusy;
    case LedStatus::Error:
      return kError;
  }
  return kOff;
}

bool LedService::shouldBlink(LedStatus status) const {
  return status == LedStatus::Boot || status == LedStatus::Connecting ||
         status == LedStatus::BlePairing || status == LedStatus::Error;
}

uint32_t LedService::blinkPeriodMs(LedStatus status) const {
  switch (status) {
    case LedStatus::Boot:
      return 160;
    case LedStatus::Connecting:
      return 300;
    case LedStatus::BlePairing:
      return 180;
    case LedStatus::Error:
      return 220;
    default:
      return 1000;
  }
}

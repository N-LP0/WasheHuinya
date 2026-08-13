#pragma once

#include <Arduino.h>
#include <USBHIDKeyboard.h>
#include <USBHIDMouse.h>

#include "AppConfig.h"

class BleHidService;

class MacroEngine {
 public:
  MacroEngine();

  void init(BleHidService* ble, const String& hidTransport);
  bool start(const String& profileName, const String& script);
  bool stop();
  bool finishAfterCurrentRun();
  bool runCommandLine(const String& commandLine);
  void tick();

  void onBleConnected();
  void onBleDisconnected();

  bool isBusy() const;
  uint32_t startCount() const;
  MacroStateView snapshot() const;

 private:
  bool runLine(const String& line, bool chunkMouse);
  bool moveMousePrecise(int x, int y, int jitter);
  bool scrollMousePrecise(int wheel, int pan);
  bool hasPendingMouseReport() const;
  void clearPendingMouseReport();
  void queueMouseMove(int x, int y, int jitter);
  void queueMouseScroll(int wheel, int pan);
  bool runPendingMouseReport();
  bool pushLoop(size_t startCursor, uint32_t repeatCount, bool infinite);
  bool handleLoopEnd();
  void clearLoopStack();
  int keyCode(const String& token) const;
  int mouseButton(const String& token) const;
  String tokenAt(const String& line, size_t& pos) const;
  String tailAt(const String& line, size_t pos) const;
  bool parseIntToken(const String& text, int& value) const;
  bool typeText(const String& text);
  bool pressKey(uint8_t key);
  void releaseKeyboard();
  bool moveMouse(int8_t x, int8_t y, int8_t wheel = 0, int8_t pan = 0);
  bool clickMouse(uint8_t button);
  bool pressMouse(uint8_t button);
  bool releaseMouse(uint8_t button);
  void releaseMouseButtons();

  mutable SemaphoreHandle_t mutex_ = nullptr;

  USBHIDKeyboard keyboard_;
  USBHIDMouse mouse_;
  BleHidService* ble_ = nullptr;
  bool useBle_ = false;

  bool busy_ = false;
  bool directCommandActive_ = false;
  bool paused_ = false;
  bool infinite_ = false;
  bool releasePending_ = false;
  String script_;
  String currentProfile_;
  size_t cursor_ = 0;
  uint32_t repeatTarget_ = 1;
  uint32_t repeatDone_ = 0;
  uint32_t startCount_ = 0;
  uint32_t waitUntil_ = 0;
  uint32_t nextMouseReportMs_ = 0;
  int pendingMouseX_ = 0;
  int pendingMouseY_ = 0;
  int pendingMouseSentX_ = 0;
  int pendingMouseSentY_ = 0;
  int pendingMouseStepIndex_ = 0;
  int pendingMouseStepCount_ = 0;
  int pendingMouseArc_ = 0;
  int pendingMouseWheel_ = 0;
  int pendingMousePan_ = 0;
  String message_ = "Ready";

  struct LoopFrame {
    size_t startCursor = 0;
    uint32_t remaining = 0;
    bool infinite = false;
  };
  static constexpr size_t kMaxLoopDepth = 8;
  LoopFrame loopStack_[kMaxLoopDepth];
  size_t loopDepth_ = 0;
};

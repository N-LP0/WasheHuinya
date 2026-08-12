#include "MacroEngine.h"

#include "BleHidService.h"

#include <esp_system.h>

namespace {
constexpr int kMouseMoveStep = 8;
constexpr uint32_t kMouseMoveStepDelayMs = 8;
constexpr int kMouseWheelStep = 1;
constexpr uint32_t kMouseWheelStepDelayMs = 24;
constexpr int kMaxMouseJitter = 12;
constexpr float kMouseBallisticsCompensation = 0.65f;

int8_t constrainedStep(int value, int maxAbs) {
  return static_cast<int8_t>(constrain(value, -maxAbs, maxAbs));
}

int mouseMoveStepCount(int x, int y) {
  const int distance = max(abs(x), abs(y));
  return distance == 0 ? 0 : (distance + kMouseMoveStep - 1) / kMouseMoveStep;
}

int roundedRatio(int value, int index, int count) {
  if (count <= 0) return value;
  const int64_t numerator = static_cast<int64_t>(value) * index;
  return static_cast<int>((numerator >= 0 ? numerator + count / 2 : numerator - count / 2) / count);
}

int signedArc(int jitter, int stepCount) {
  if (jitter <= 0 || stepCount < 3) return 0;
  return (esp_random() & 1U) == 0 ? jitter : -jitter;
}

int arcOffset(int arc, int index, int count) {
  if (arc == 0 || count <= 0 || index <= 0 || index >= count) return 0;
  const int64_t numerator = static_cast<int64_t>(arc) * 4 * index * (count - index);
  const int64_t denominator = static_cast<int64_t>(count) * count;
  return static_cast<int>((numerator >= 0 ? numerator + denominator / 2
                                          : numerator - denominator / 2) /
                          denominator);
}

int compensatedTravel(int distance, int index, int count) {
  if (distance <= 0 || count <= 0) return 0;
  if (index >= count) return distance;
  if (index <= 0) return 0;
  const float t = static_cast<float>(index) / static_cast<float>(count);
  const float shaped = t + kMouseBallisticsCompensation * t * (1.0f - t);
  return constrain(static_cast<int>(roundf(distance * shaped)), 0, distance);
}

void desiredArcPoint(int targetX, int targetY, int arc, int index, int count, int& x, int& y) {
  const int offset = arcOffset(arc, index, count);
  if (abs(targetX) >= abs(targetY)) {
    const int distance = abs(targetX);
    const int traveled = compensatedTravel(distance, index, count);
    x = targetX < 0 ? -traveled : traveled;
    y = distance == 0 ? targetY : roundedRatio(targetY, traveled, distance);
    y += offset;
  } else {
    const int distance = abs(targetY);
    const int traveled = compensatedTravel(distance, index, count);
    y = targetY < 0 ? -traveled : traveled;
    x = distance == 0 ? targetX : roundedRatio(targetX, traveled, distance);
    x += offset;
  }
}
}  // namespace

MacroEngine::MacroEngine() = default;

void MacroEngine::init(BleHidService* ble, const String& hidTransport) {
  mutex_ = xSemaphoreCreateMutex();
  ble_ = ble;
  useBle_ = hidTransport == "ble";
  keyboard_.begin();
  mouse_.begin();
}

bool MacroEngine::start(const String& profileName, const String& script) {
  if (script.isEmpty()) {
    xSemaphoreTake(mutex_, portMAX_DELAY);
    message_ = "Profile is empty";
    xSemaphoreGive(mutex_);
    return false;
  }

  xSemaphoreTake(mutex_, portMAX_DELAY);
  if (busy_) {
    xSemaphoreGive(mutex_);
    return false;
  }
  busy_ = true;
  infinite_ = false;
  releasePending_ = false;
  script_ = script;
  currentProfile_ = profileName;
  cursor_ = 0;
  repeatTarget_ = 1;
  repeatDone_ = 0;
  waitUntil_ = 0;
  nextMouseReportMs_ = 0;
  clearPendingMouseReport();
  clearLoopStack();
  message_ = String("Running: ") + profileName;
  ++startCount_;
  xSemaphoreGive(mutex_);
  return true;
}

bool MacroEngine::stop() {
  xSemaphoreTake(mutex_, portMAX_DELAY);
  if (!busy_) {
    xSemaphoreGive(mutex_);
    return false;
  }

  busy_ = false;
  infinite_ = false;
  releasePending_ = false;
  releaseKeyboard();
  releaseMouseButtons();
  script_ = "";
  currentProfile_ = "";
  cursor_ = 0;
  repeatTarget_ = 1;
  repeatDone_ = 0;
  waitUntil_ = 0;
  nextMouseReportMs_ = 0;
  clearPendingMouseReport();
  clearLoopStack();
  message_ = "Stopped";
  xSemaphoreGive(mutex_);
  return true;
}

bool MacroEngine::finishAfterCurrentRun() {
  xSemaphoreTake(mutex_, portMAX_DELAY);
  if (!busy_) {
    xSemaphoreGive(mutex_);
    return false;
  }

  infinite_ = false;
  repeatTarget_ = repeatDone_ + 1;
  message_ = String("Finishing: ") + currentProfile_;
  xSemaphoreGive(mutex_);
  return true;
}

bool MacroEngine::runCommandLine(const String& commandLine) {
  xSemaphoreTake(mutex_, portMAX_DELAY);
  const bool canRun = !busy_;
  xSemaphoreGive(mutex_);
  if (!canRun) {
    return false;
  }
  return runLine(commandLine, false);
}

void MacroEngine::tick() {
  xSemaphoreTake(mutex_, portMAX_DELAY);

  if (releasePending_ && millis() >= waitUntil_) {
    releaseKeyboard();
    releasePending_ = false;
    waitUntil_ = 0;
  }

  if (!busy_ || releasePending_) {
    xSemaphoreGive(mutex_);
    return;
  }

  if (waitUntil_ != 0 && millis() < waitUntil_) {
    xSemaphoreGive(mutex_);
    return;
  }

  if (hasPendingMouseReport()) {
    if (!runPendingMouseReport()) {
      busy_ = false;
      infinite_ = false;
      releasePending_ = false;
      currentProfile_ = "";
      script_ = "";
      cursor_ = 0;
      repeatTarget_ = 1;
      repeatDone_ = 0;
      waitUntil_ = 0;
      nextMouseReportMs_ = 0;
      clearLoopStack();
      message_ = "BLE HID is not connected or mode is incompatible";
    }
    xSemaphoreGive(mutex_);
    return;
  }

  while (cursor_ < script_.length()) {
    int end = script_.indexOf('\n', cursor_);
    if (end < 0) {
      end = script_.length();
    }
    String line = script_.substring(cursor_, end);
    cursor_ = (static_cast<size_t>(end) < script_.length()) ? static_cast<size_t>(end) + 1
                                                            : script_.length();
    line.trim();

    if (line.isEmpty() || line.startsWith("#") || line.startsWith(";") || line.startsWith("//")) {
      continue;
    }

    if (!runLine(line, true)) {
      busy_ = false;
      infinite_ = false;
      releasePending_ = false;
      releaseKeyboard();
      releaseMouseButtons();
      script_ = "";
      currentProfile_ = "";
      cursor_ = 0;
      repeatTarget_ = 1;
      repeatDone_ = 0;
      nextMouseReportMs_ = 0;
      clearPendingMouseReport();
      clearLoopStack();
      message_ = "Macro error";
      xSemaphoreGive(mutex_);
      return;
    }

    if (waitUntil_ != 0) {
      xSemaphoreGive(mutex_);
      return;
    }
  }

  ++repeatDone_;

  busy_ = false;
  infinite_ = false;
  releasePending_ = false;
  releaseKeyboard();
  releaseMouseButtons();
  message_ = "Done";
  currentProfile_ = "";
  script_ = "";
  cursor_ = 0;
  repeatTarget_ = 1;
  repeatDone_ = 0;
  nextMouseReportMs_ = 0;
  clearPendingMouseReport();
  clearLoopStack();

  xSemaphoreGive(mutex_);
}

bool MacroEngine::isBusy() const {
  xSemaphoreTake(mutex_, portMAX_DELAY);
  const bool busy = busy_;
  xSemaphoreGive(mutex_);
  return busy;
}

uint32_t MacroEngine::startCount() const {
  xSemaphoreTake(mutex_, portMAX_DELAY);
  const uint32_t count = startCount_;
  xSemaphoreGive(mutex_);
  return count;
}

MacroStateView MacroEngine::snapshot() const {
  xSemaphoreTake(mutex_, portMAX_DELAY);
  MacroStateView view;
  view.busy = busy_;
  view.infinite = infinite_;
  view.repeatTarget = repeatTarget_;
  view.repeatDone = repeatDone_;
  view.currentProfile = currentProfile_;
  view.message = message_;
  xSemaphoreGive(mutex_);
  return view;
}

bool MacroEngine::runLine(const String& line, bool chunkMouse) {
  String s = line;
  s.trim();
  if (s.isEmpty()) {
    return true;
  }

  size_t pos = 0;
  String cmd = tokenAt(s, pos);
  cmd.toUpperCase();
  String arg = tailAt(s, pos);

  if (cmd == "TEXT" || cmd == "TYPE") {
    if (!typeText(arg)) return false;
    waitUntil_ = millis() + 10;
    return true;
  }

  if (cmd == "DELAY" || cmd == "WAIT") {
    int ms = 0;
    if (!parseIntToken(arg, ms) || ms < 0) {
      return false;
    }
    waitUntil_ = millis() + static_cast<uint32_t>(ms);
    return true;
  }

  if (cmd == "KEY" || cmd == "HOTKEY") {
    size_t p = 0;
    String first = tokenAt(arg, p);
    if (first.isEmpty()) {
      return false;
    }

    const bool printable = first.length() == 1 && keyCode(first) < 0;
    if (printable && p >= arg.length()) {
      if (!pressKey(static_cast<uint8_t>(first[0]))) return false;
      releaseKeyboard();
      return true;
    }

    int mods[4];
    size_t modCount = 0;
    int primary = -1;

    auto absorb = [&](const String& token) {
      String t = token;
      t.toUpperCase();
      if (t == "CTRL" || t == "CONTROL") {
        if (modCount < 4)
          mods[modCount++] = KEY_LEFT_CTRL;
        return;
      }
      if (t == "SHIFT") {
        if (modCount < 4)
          mods[modCount++] = KEY_LEFT_SHIFT;
        return;
      }
      if (t == "ALT") {
        if (modCount < 4)
          mods[modCount++] = KEY_LEFT_ALT;
        return;
      }
      if (t == "GUI" || t == "WIN" || t == "CMD") {
        if (modCount < 4)
          mods[modCount++] = KEY_LEFT_GUI;
        return;
      }
      const int code = keyCode(t);
      if (code >= 0) {
        primary = code;
        return;
      }
      if (t.length() == 1) {
        primary = static_cast<uint8_t>(t[0]);
      }
    };

    absorb(first);
    while (p < arg.length()) {
      String t = tokenAt(arg, p);
      if (t.isEmpty())
        break;
      absorb(t);
    }

    if (primary < 0) {
      return false;
    }

    for (size_t i = 0; i < modCount; ++i) {
      if (!pressKey(static_cast<uint8_t>(mods[i]))) return false;
    }
    if (!pressKey(static_cast<uint8_t>(primary))) return false;
    releasePending_ = true;
    waitUntil_ = millis() + 35;
    return true;
  }

  if (cmd == "REPEAT") {
    if (!chunkMouse) {
      message_ = "REPEAT is only valid inside a macro";
      return false;
    }
    int count = 0;
    if (!parseIntToken(arg, count) || count <= 0) {
      message_ = "REPEAT requires a positive count";
      return false;
    }
    return pushLoop(cursor_, static_cast<uint32_t>(count), false);
  }

  if (cmd == "LOOP") {
    if (!chunkMouse) {
      message_ = "LOOP is only valid inside a macro";
      return false;
    }
    return pushLoop(cursor_, 0, true);
  }

  if (cmd == "END") {
    if (!chunkMouse) {
      message_ = "END is only valid inside a macro";
      return false;
    }
    return handleLoopEnd();
  }

  if (cmd == "MOUSE") {
    size_t p = 0;
    String sub = tokenAt(arg, p);
    sub.toUpperCase();

    if (sub == "MOVE") {
      String xs = tokenAt(arg, p);
      String ys = tokenAt(arg, p);
      int x = 0;
      int y = 0;
      int jitter = 0;
      if (!parseIntToken(xs, x) || !parseIntToken(ys, y)) {
        return false;
      }
      const String jitterToken = tokenAt(arg, p);
      if (!jitterToken.isEmpty() && !parseIntToken(jitterToken, jitter)) {
        return false;
      }
      jitter = constrain(jitter, 0, kMaxMouseJitter);
      if (chunkMouse) {
        queueMouseMove(x, y, jitter);
        if (!runPendingMouseReport()) return false;
      } else {
        if (!moveMousePrecise(x, y, jitter)) return false;
        waitUntil_ = millis() + 20;
      }
      return true;
    }

    if (sub == "WHEEL" || sub == "SCROLL") {
      int wheel = 0;
      if (!parseIntToken(tokenAt(arg, p), wheel)) {
        return false;
      }
      if (chunkMouse) {
        queueMouseScroll(wheel, 0);
        if (!runPendingMouseReport()) return false;
      } else {
        if (!scrollMousePrecise(wheel, 0)) return false;
        waitUntil_ = millis() + 8;
      }
      return true;
    }

    if (sub == "PAN" || sub == "HWHEEL") {
      int pan = 0;
      if (!parseIntToken(tokenAt(arg, p), pan)) {
        return false;
      }
      if (chunkMouse) {
        queueMouseScroll(0, pan);
        if (!runPendingMouseReport()) return false;
      } else {
        if (!scrollMousePrecise(0, pan)) return false;
        waitUntil_ = millis() + 8;
      }
      return true;
    }

    if (sub == "CLICK") {
      const int button = mouseButton(tokenAt(arg, p));
      if (button < 0) {
        return false;
      }
      return clickMouse(static_cast<uint8_t>(button));
    }

    if (sub == "PRESS") {
      const int button = mouseButton(tokenAt(arg, p));
      if (button < 0) {
        return false;
      }
      return pressMouse(static_cast<uint8_t>(button));
    }

    if (sub == "RELEASE") {
      const int button = mouseButton(tokenAt(arg, p));
      if (button < 0) {
        return false;
      }
      return releaseMouse(static_cast<uint8_t>(button));
    }
  }

  if (cmd == "RELEASEALL") {
    releaseKeyboard();
    releaseMouseButtons();
    return true;
  }

  message_ = String("Unknown command: ") + cmd;
  return false;
}

bool MacroEngine::moveMousePrecise(int x, int y, int jitter) {
  jitter = constrain(jitter, 0, kMaxMouseJitter);
  const int stepCount = mouseMoveStepCount(x, y);
  const int arc = signedArc(jitter, stepCount);
  int sentX = 0;
  int sentY = 0;
  for (int i = 1; i <= stepCount; ++i) {
    int desiredX = 0;
    int desiredY = 0;
    desiredArcPoint(x, y, arc, i, stepCount, desiredX, desiredY);
    const int stepX = desiredX - sentX;
    const int stepY = desiredY - sentY;
    if (!moveMouse(stepX, stepY)) return false;
    sentX = desiredX;
    sentY = desiredY;
    if (i < stepCount) delay(kMouseMoveStepDelayMs);
  }
  return true;
}

bool MacroEngine::scrollMousePrecise(int wheel, int pan) {
  while (wheel != 0 || pan != 0) {
    const int8_t stepWheel = constrainedStep(wheel, kMouseWheelStep);
    const int8_t stepPan = constrainedStep(pan, kMouseWheelStep);
    if (!moveMouse(0, 0, stepWheel, stepPan)) return false;
    wheel -= stepWheel;
    pan -= stepPan;
    if (wheel != 0 || pan != 0) delay(kMouseWheelStepDelayMs);
  }
  return true;
}

bool MacroEngine::hasPendingMouseReport() const {
  return pendingMouseStepIndex_ < pendingMouseStepCount_ || pendingMouseWheel_ != 0 ||
         pendingMousePan_ != 0;
}

void MacroEngine::clearPendingMouseReport() {
  pendingMouseX_ = 0;
  pendingMouseY_ = 0;
  pendingMouseSentX_ = 0;
  pendingMouseSentY_ = 0;
  pendingMouseStepIndex_ = 0;
  pendingMouseStepCount_ = 0;
  pendingMouseArc_ = 0;
  nextMouseReportMs_ = 0;
  pendingMouseWheel_ = 0;
  pendingMousePan_ = 0;
}

void MacroEngine::queueMouseMove(int x, int y, int jitter) {
  pendingMouseX_ = x;
  pendingMouseY_ = y;
  pendingMouseSentX_ = 0;
  pendingMouseSentY_ = 0;
  pendingMouseStepIndex_ = 0;
  pendingMouseStepCount_ = mouseMoveStepCount(x, y);
  pendingMouseArc_ = signedArc(constrain(jitter, 0, kMaxMouseJitter), pendingMouseStepCount_);
  nextMouseReportMs_ = millis();
  pendingMouseWheel_ = 0;
  pendingMousePan_ = 0;
}

void MacroEngine::queueMouseScroll(int wheel, int pan) {
  pendingMouseX_ = 0;
  pendingMouseY_ = 0;
  pendingMouseSentX_ = 0;
  pendingMouseSentY_ = 0;
  pendingMouseStepIndex_ = 0;
  pendingMouseStepCount_ = 0;
  pendingMouseArc_ = 0;
  nextMouseReportMs_ = millis();
  pendingMouseWheel_ = wheel;
  pendingMousePan_ = pan;
}

bool MacroEngine::runPendingMouseReport() {
  int stepX = 0;
  int stepY = 0;
  if (pendingMouseStepIndex_ < pendingMouseStepCount_) {
    int desiredX = 0;
    int desiredY = 0;
    desiredArcPoint(pendingMouseX_,
                    pendingMouseY_,
                    pendingMouseArc_,
                    pendingMouseStepIndex_ + 1,
                    pendingMouseStepCount_,
                    desiredX,
                    desiredY);
    stepX = desiredX - pendingMouseSentX_;
    stepY = desiredY - pendingMouseSentY_;
  }
  const int8_t stepWheel = constrainedStep(pendingMouseWheel_, kMouseWheelStep);
  const int8_t stepPan = constrainedStep(pendingMousePan_, kMouseWheelStep);

  if (!moveMouse(stepX, stepY, stepWheel, stepPan)) {
    clearPendingMouseReport();
    message_ = "BLE HID is not connected or mode is incompatible";
    return false;
  }
  pendingMouseSentX_ += stepX;
  pendingMouseSentY_ += stepY;
  if (pendingMouseStepIndex_ < pendingMouseStepCount_) ++pendingMouseStepIndex_;
  pendingMouseWheel_ -= stepWheel;
  pendingMousePan_ -= stepPan;
  const uint32_t interval = stepWheel != 0 || stepPan != 0 ? kMouseWheelStepDelayMs
                                                           : kMouseMoveStepDelayMs;
  nextMouseReportMs_ += interval;
  const uint32_t now = millis();
  waitUntil_ = static_cast<int32_t>(nextMouseReportMs_ - now) > 0 ? nextMouseReportMs_ : 0;
  return true;
}

bool MacroEngine::pushLoop(size_t startCursor, uint32_t repeatCount, bool infinite) {
  if (loopDepth_ >= kMaxLoopDepth) {
    message_ = "Loop nesting is too deep";
    return false;
  }
  LoopFrame& frame = loopStack_[loopDepth_++];
  frame.startCursor = startCursor;
  frame.remaining = repeatCount;
  frame.infinite = infinite;
  if (infinite) {
    infinite_ = true;
  }
  return true;
}

bool MacroEngine::handleLoopEnd() {
  if (loopDepth_ == 0) {
    message_ = "END without REPEAT or LOOP";
    return false;
  }

  LoopFrame& frame = loopStack_[loopDepth_ - 1];
  if (frame.infinite) {
    if (infinite_) {
      cursor_ = frame.startCursor;
      return true;
    }
    --loopDepth_;
    return true;
  }

  if (frame.remaining > 1) {
    --frame.remaining;
    cursor_ = frame.startCursor;
    return true;
  }

  --loopDepth_;
  return true;
}

void MacroEngine::clearLoopStack() {
  loopDepth_ = 0;
}

int MacroEngine::keyCode(const String& token) const {
  String t = token;
  t.toUpperCase();
  if (t == "ENTER" || t == "RETURN")
    return KEY_RETURN;
  if (t == "ESC" || t == "ESCAPE")
    return KEY_ESC;
  if (t == "TAB")
    return KEY_TAB;
  if (t == "SPACE")
    return 0x20;
  if (t == "BACKSPACE")
    return KEY_BACKSPACE;
  if (t == "DEL" || t == "DELETE")
    return KEY_DELETE;
  if (t == "INS" || t == "INSERT")
    return KEY_INSERT;
  if (t == "UP")
    return KEY_UP_ARROW;
  if (t == "DOWN")
    return KEY_DOWN_ARROW;
  if (t == "LEFT")
    return KEY_LEFT_ARROW;
  if (t == "RIGHT")
    return KEY_RIGHT_ARROW;
  if (t == "HOME")
    return KEY_HOME;
  if (t == "END")
    return KEY_END;
  if (t == "PGUP")
    return KEY_PAGE_UP;
  if (t == "PGDN")
    return KEY_PAGE_DOWN;
  if (t == "F1")
    return KEY_F1;
  if (t == "F2")
    return KEY_F2;
  if (t == "F3")
    return KEY_F3;
  if (t == "F4")
    return KEY_F4;
  if (t == "F5")
    return KEY_F5;
  if (t == "F6")
    return KEY_F6;
  if (t == "F7")
    return KEY_F7;
  if (t == "F8")
    return KEY_F8;
  if (t == "F9")
    return KEY_F9;
  if (t == "F10")
    return KEY_F10;
  if (t == "F11")
    return KEY_F11;
  if (t == "F12")
    return KEY_F12;
  return -1;
}

int MacroEngine::mouseButton(const String& token) const {
  String t = token;
  t.toUpperCase();
  if (t == "LEFT")
    return MOUSE_LEFT;
  if (t == "RIGHT")
    return MOUSE_RIGHT;
  if (t == "MIDDLE")
    return MOUSE_MIDDLE;
  if (t == "WHEEL" || t == "MBUTTON" || t == "MOUSE3")
    return MOUSE_MIDDLE;
  return -1;
}

String MacroEngine::tokenAt(const String& line, size_t& pos) const {
  while (pos < line.length() && isspace(static_cast<unsigned char>(line[pos]))) {
    ++pos;
  }
  const size_t start = pos;
  while (pos < line.length() && !isspace(static_cast<unsigned char>(line[pos]))) {
    ++pos;
  }
  return line.substring(start, pos);
}

String MacroEngine::tailAt(const String& line, size_t pos) const {
  while (pos < line.length() && isspace(static_cast<unsigned char>(line[pos]))) {
    ++pos;
  }
  return line.substring(pos);
}

bool MacroEngine::parseIntToken(const String& text, int& value) const {
  String token = text;
  token.trim();
  if (token.isEmpty()) {
    return false;
  }
  char* end = nullptr;
  value = strtol(token.c_str(), &end, 10);
  return end != token.c_str() && *end == '\0';
}

bool MacroEngine::typeText(const String& text) {
  if (useBle_) return ble_ && ble_->typeText(text);
  keyboard_.print(text.c_str());
  return true;
}

bool MacroEngine::pressKey(uint8_t key) {
  if (useBle_) return ble_ && ble_->pressKey(key);
  return keyboard_.press(key) != 0;
}

void MacroEngine::releaseKeyboard() {
  if (useBle_) {
    if (ble_) ble_->releaseKeyboard();
  } else {
    keyboard_.releaseAll();
  }
}

bool MacroEngine::moveMouse(int8_t x, int8_t y, int8_t wheel, int8_t pan) {
  if (useBle_) return ble_ && ble_->moveMouse(x, y, wheel, pan);
  mouse_.move(x, y, wheel, pan);
  return true;
}

bool MacroEngine::clickMouse(uint8_t button) {
  if (useBle_) return ble_ && ble_->clickMouse(button);
  mouse_.click(button);
  return true;
}

bool MacroEngine::pressMouse(uint8_t button) {
  if (useBle_) return ble_ && ble_->pressMouse(button);
  mouse_.press(button);
  return true;
}

bool MacroEngine::releaseMouse(uint8_t button) {
  if (useBle_) return ble_ && ble_->releaseMouse(button);
  mouse_.release(button);
  return true;
}

void MacroEngine::releaseMouseButtons() {
  if (useBle_) {
    if (ble_) ble_->releaseMouseButtons();
  } else {
    mouse_.release(MOUSE_LEFT);
    mouse_.release(MOUSE_RIGHT);
    mouse_.release(MOUSE_MIDDLE);
  }
}

#pragma once

#include <Arduino.h>

#include <functional>

class CommandService {
 public:
  using ExecuteMacroCallback = std::function<bool(const String&)>;
  using ExecuteLineCallback = std::function<bool(const String&)>;
  using StopMacroCallback = std::function<bool()>;

  bool init();

  void setExecuteMacroCallback(ExecuteMacroCallback cb);
  void setExecuteLineCallback(ExecuteLineCallback cb);
  void setStopMacroCallback(StopMacroCallback cb);

  String execute(const String& raw);

  String getLog() const;
  uint32_t getLogVersion() const;
  void appendLog(const String& line, const char* level = "INFO");

 private:
  String trimCommand(String cmd) const;

  mutable SemaphoreHandle_t mutex_ = nullptr;
  String ttyLog_;
  uint32_t logVersion_ = 0;
  ExecuteMacroCallback executeMacro_;
  ExecuteLineCallback executeLine_;
  StopMacroCallback stopMacro_;
};

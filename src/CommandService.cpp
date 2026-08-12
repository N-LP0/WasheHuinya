#include "CommandService.h"

bool CommandService::init() {
  mutex_ = xSemaphoreCreateMutex();
  if (mutex_ == nullptr) {
    return false;
  }
  ttyLog_.reserve(4096);
  appendLog("TTY ready");
  return true;
}

void CommandService::setExecuteMacroCallback(ExecuteMacroCallback cb) {
  executeMacro_ = cb;
}

void CommandService::setExecuteLineCallback(ExecuteLineCallback cb) {
  executeLine_ = cb;
}

void CommandService::setStopMacroCallback(StopMacroCallback cb) {
  stopMacro_ = cb;
}

String CommandService::execute(const String& raw) {
  const String cmd = trimCommand(raw);
  if (cmd.isEmpty()) {
    appendLog("ERR empty command", "WARN");
    return "ERR empty command";
  }

  appendLog(String("> ") + cmd);

  String upper = cmd;
  upper.toUpperCase();

  if (upper == "PING") {
    appendLog("PONG");
    return "PONG";
  }

  if (upper.startsWith("RUN ")) {
    if (!executeMacro_) {
      appendLog("ERR callback not set", "ERROR");
      return "ERR callback not set";
    }
    String args = cmd.substring(4);
    args.trim();
    if (args.isEmpty() || args.indexOf(' ') >= 0) {
      appendLog("ERR invalid run syntax", "WARN");
      return "ERR invalid run syntax";
    }

    const bool ok = executeMacro_(args);
    const String out = ok ? "OK started" : "ERR start failed";
    appendLog(out, ok ? "INFO" : "ERROR");
    return out;
  }

  if (upper == "STOP") {
    if (!stopMacro_) {
      appendLog("ERR callback not set", "ERROR");
      return "ERR callback not set";
    }
    const bool ok = stopMacro_();
    const String out = ok ? "OK stopped" : "ERR not running";
    appendLog(out, ok ? "INFO" : "WARN");
    return out;
  }

  if (upper.startsWith("LINE ")) {
    if (!executeLine_) {
      appendLog("ERR callback not set", "ERROR");
      return "ERR callback not set";
    }
    String line = cmd.substring(5);
    line.trim();
    const bool ok = executeLine_(line);
    const String out = ok ? "OK line executed" : "ERR line failed";
    appendLog(out, ok ? "INFO" : "ERROR");
    return out;
  }

  if (upper == "HELP") {
    const String out = "HELP | PING | RUN <profile> | STOP | LINE <macro_line>";
    appendLog(out);
    return out;
  }

  appendLog("ERR unknown command", "WARN");
  return "ERR unknown command";
}

String CommandService::getLog() const {
  xSemaphoreTake(mutex_, portMAX_DELAY);
  const String out = ttyLog_;
  xSemaphoreGive(mutex_);
  return out;
}

uint32_t CommandService::getLogVersion() const {
  xSemaphoreTake(mutex_, portMAX_DELAY);
  const uint32_t version = logVersion_;
  xSemaphoreGive(mutex_);
  return version;
}

void CommandService::appendLog(const String& line, const char* level) {
  xSemaphoreTake(mutex_, portMAX_DELAY);
  ttyLog_ += "[";
  ttyLog_ += level == nullptr ? "INFO" : level;
  ttyLog_ += "] ";
  ttyLog_ += line;
  ttyLog_ += "\n";
  if (ttyLog_.length() > 7000) {
    ttyLog_.remove(0, 3000);
  }
  ++logVersion_;
  xSemaphoreGive(mutex_);
}

String CommandService::trimCommand(String cmd) const {
  cmd.replace("\r", "");
  cmd.replace("\n", " ");
  cmd.trim();
  return cmd;
}

#include "WebApiServer.h"

#include "AppLimits.h"

#include <algorithm>
#include <cerrno>
#include <climits>
#include <utility>

#include <ArduinoJson.h>
#include <LittleFS.h>
#include <esp_ota_ops.h>
#include <mbedtls/base64.h>
#include <mbedtls/sha1.h>

namespace {
#ifndef HIDPAD_FIRMWARE_VERSION
#define HIDPAD_FIRMWARE_VERSION "dev"
#endif
constexpr char kFirmwareVersion[] = HIDPAD_FIRMWARE_VERSION;
const char* kCollectedHeaders[] = {
    "X-TTY-Token",
    "X-Update-Size",
    "X-Update-SHA256",
    "X-Restart-After-Update",
    "X-Bundle-ID",
};

bool parseSizeHeader(const String& value, size_t& size) {
  if (value.isEmpty()) return false;
  char* end = nullptr;
  errno = 0;
  const unsigned long parsed = strtoul(value.c_str(), &end, 10);
  if (errno == ERANGE || end == value.c_str() || *end != '\0' || parsed == 0 ||
      parsed > SIZE_MAX) {
    return false;
  }
  size = static_cast<size_t>(parsed);
  return true;
}
}

WebApiServer::WebApiServer(StorageService& storage,
                           WifiService& wifi,
                           MacroEngine& macro,
                           BleHidService& ble,
                           CommandService& cmd,
                           GpioService& gpio,
                           UpdateService& update)
    : storage_(storage),
      wifi_(wifi),
      macro_(macro),
      ble_(ble),
      cmd_(cmd),
      gpio_(gpio),
      update_(update),
      server_(80),
      webSocketServer_(81) {}

void WebApiServer::init() {
  server_.collectHeaders(kCollectedHeaders, sizeof(kCollectedHeaders) / sizeof(kCollectedHeaders[0]));
  registerRoutes();
  registerWebSocket();
  server_.begin();
  Serial.println("HTTP server started on port 80");
}

void WebApiServer::handleClient() {
  server_.handleClient();
  handleWebSocketClients();
  broadcastTtyLogIfChanged();
  if (restartPending_ && static_cast<int32_t>(millis() - restartAtMs_) >= 0) {
    ESP.restart();
  }
}

void WebApiServer::registerRoutes() {
  server_.on("/", HTTP_GET, [this]() { handleRoot(); });
  server_.on("/api/ping", HTTP_GET, [this]() { handlePing(); });
  server_.on("/api/observability", HTTP_GET, [this]() { handleObservability(); });
  server_.on("/generate_204", HTTP_GET, [this]() { redirectToRoot(); });
  server_.on("/gen_204", HTTP_GET, [this]() { redirectToRoot(); });
  server_.on("/hotspot-detect.html", HTTP_GET, [this]() { redirectToRoot(); });
  server_.on("/library/test/success.html", HTTP_GET, [this]() { redirectToRoot(); });
  server_.on("/ncsi.txt", HTTP_GET, [this]() { redirectToRoot(); });
  server_.on("/connecttest.txt", HTTP_GET, [this]() { redirectToRoot(); });
  server_.on("/redirect", HTTP_GET, [this]() { redirectToRoot(); });

  server_.on("/style/theme.css", HTTP_GET,
             [this]() { streamFile("/style/theme.css", "text/css; charset=utf-8"); });
  server_.on("/style/layout.css", HTTP_GET,
             [this]() { streamFile("/style/layout.css", "text/css; charset=utf-8"); });
  server_.on("/style/forms.css", HTTP_GET,
             [this]() { streamFile("/style/forms.css", "text/css; charset=utf-8"); });
  server_.on("/style/terminal.css", HTTP_GET,
             [this]() { streamFile("/style/terminal.css", "text/css; charset=utf-8"); });
  server_.on("/style/responsive.css", HTTP_GET,
             [this]() { streamFile("/style/responsive.css", "text/css; charset=utf-8"); });

  server_.on("/script/api.js", HTTP_GET,
             [this]() { streamFile("/script/api.js", "application/javascript; charset=utf-8"); });
  server_.on("/script/ui.js", HTTP_GET,
             [this]() { streamFile("/script/ui.js", "application/javascript; charset=utf-8"); });
  server_.on("/script/wifi.js", HTTP_GET,
             [this]() { streamFile("/script/wifi.js", "application/javascript; charset=utf-8"); });
  server_.on("/script/gpio.js", HTTP_GET,
             [this]() { streamFile("/script/gpio.js", "application/javascript; charset=utf-8"); });
  server_.on("/script/ble.js", HTTP_GET,
             [this]() { streamFile("/script/ble.js", "application/javascript; charset=utf-8"); });
  server_.on("/script/profiles.js", HTTP_GET,
             [this]() { streamFile("/script/profiles.js", "application/javascript; charset=utf-8"); });
  server_.on("/script/tty.js", HTTP_GET,
             [this]() { streamFile("/script/tty.js", "application/javascript; charset=utf-8"); });
  server_.on("/script/update.js", HTTP_GET,
             [this]() { streamFile("/script/update.js", "application/javascript; charset=utf-8"); });
  server_.on("/script/app.js", HTTP_GET,
             [this]() { streamFile("/script/app.js", "application/javascript; charset=utf-8"); });
  server_.on("/api/status", HTTP_GET, [this]() { handleStatus(); });
  server_.on("/api/profile/load", HTTP_GET, [this]() { handleProfileLoad(); });
  server_.on("/api/profile/save", HTTP_POST, [this]() { handleProfileSave(); });
  server_.on("/api/profile/export", HTTP_GET, [this]() { handleProfileExport(); });
  server_.on("/api/profile/import", HTTP_POST, [this]() { handleProfileImport(); });
  server_.on("/api/profile/delete", HTTP_POST, [this]() { handleProfileDelete(); });
  server_.on("/api/run", HTTP_POST, [this]() { handleRun(); });
  server_.on("/api/run/script", HTTP_POST, [this]() { handleRunScript(); });
  server_.on("/api/stop", HTTP_POST, [this]() { handleStop(); });
  server_.on("/api/wifi/save", HTTP_POST, [this]() { handleWifiSave(); });
  server_.on("/api/wifi/reset", HTTP_POST, [this]() { handleWifiReset(); });
  server_.on("/api/ble/save", HTTP_POST, [this]() { handleBleSave(); });
  server_.on("/api/ble/bond/delete", HTTP_POST, [this]() { handleBleBondDelete(); });
  server_.on("/api/ble/bonds/clear", HTTP_POST, [this]() { handleBleBondsClear(); });
  server_.on("/api/gpio/save", HTTP_POST, [this]() { handleGpioSave(); });
  server_.on("/api/gpio/delete", HTTP_POST, [this]() { handleGpioDelete(); });
  server_.on("/api/tty/exec", HTTP_POST, [this]() { handleTtyExec(); });
  server_.on("/api/tty/log", HTTP_GET, [this]() { handleTtyLog(); });
  server_.on("/api/update/firmware",
             HTTP_POST,
             [this]() { handleUpdateDone("Firmware"); },
             [this]() { handleUpdateUpload(UpdateService::UpdateType::Firmware); });
  server_.on("/api/update/filesystem",
             HTTP_POST,
             [this]() { handleUpdateDone("Filesystem"); },
             [this]() { handleUpdateUpload(UpdateService::UpdateType::Filesystem); });
  server_.on("/api/update/progress", HTTP_GET, [this]() { handleUpdateProgress(); });
  server_.onNotFound([this]() {
    if (!server_.uri().startsWith("/api/")) {
      redirectToRoot();
      return;
    }
    sendJsonError(404, "not_found", "Route was not found");
  });
}

void WebApiServer::registerWebSocket() {
  webSocketServer_.begin();
}

void WebApiServer::streamFile(const char* path, const char* mimeType) {
  if (!storage_.filesystemReady()) {
    sendJsonError(503, "storage_unavailable", "LittleFS is not mounted");
    return;
  }

  File file = LittleFS.open(path, FILE_READ);
  if (!file) {
    server_.send(404, "text/plain", String(path) + " not found");
    return;
  }

  server_.sendHeader("Cache-Control", "public, max-age=60");
  server_.streamFile(file, mimeType);
  file.close();
}

void WebApiServer::handleWebSocketClients() {
  WiFiClient client = webSocketServer_.available();
  if (client) {
    acceptWebSocketClient(client);
  }

  for (WiFiClient& wsClient : webSocketClients_) {
    if (!wsClient) {
      continue;
    }
    if (!wsClient.connected()) {
      wsClient.stop();
      continue;
    }

    while (wsClient.available()) {
      const uint8_t byte = wsClient.read();
      if ((byte & 0x0F) == 0x08) {
        wsClient.stop();
        break;
      }
    }
  }
}

void WebApiServer::acceptWebSocketClient(WiFiClient client) {
  if (!performWebSocketHandshake(client)) {
    client.stop();
    return;
  }

  for (WiFiClient& wsClient : webSocketClients_) {
    if (!wsClient || !wsClient.connected()) {
      wsClient.stop();
      wsClient = client;
      String log = cmd_.getLog();
      sendWebSocketText(wsClient, log);
      return;
    }
  }

  client.stop();
}

bool WebApiServer::performWebSocketHandshake(WiFiClient& client) {
  const uint32_t deadline = millis() + 300;
  while (!client.available() && millis() < deadline) {
    vTaskDelay(pdMS_TO_TICKS(1));
  }

  String key;
  String token;
  bool firstLine = true;
  while (client.connected() && client.available()) {
    String line = client.readStringUntil('\n');
    line.trim();
    if (line.isEmpty()) {
      break;
    }

    if (firstLine) {
      token = tokenFromRequestLine(line);
      firstLine = false;
      continue;
    }

    const String prefix = "Sec-WebSocket-Key:";
    if (line.startsWith(prefix)) {
      key = line.substring(prefix.length());
      key.trim();
    }
  }

  if (key.isEmpty() || !isTtyTokenValid(token)) {
    client.print("HTTP/1.1 401 Unauthorized\r\n");
    client.print("Content-Type: application/json\r\n");
    client.print("Connection: close\r\n\r\n");
    client.print("{\"error\":{\"code\":\"tty_unauthorized\",\"message\":\"TTY token is invalid\"}}");
    return false;
  }

  client.print("HTTP/1.1 101 Switching Protocols\r\n");
  client.print("Upgrade: websocket\r\n");
  client.print("Connection: Upgrade\r\n");
  client.print("Sec-WebSocket-Accept: ");
  client.print(webSocketAcceptKey(key));
  client.print("\r\n\r\n");
  return true;
}

bool WebApiServer::isTtyTokenValid(const String& token) {
  const DeviceConfig cfg = storage_.loadConfig();
  return cfg.ttyPassword.isEmpty() || token == cfg.ttyPassword;
}

String WebApiServer::tokenFromRequestLine(const String& requestLine) const {
  const int firstSpace = requestLine.indexOf(' ');
  if (firstSpace < 0) {
    return "";
  }

  const int secondSpace = requestLine.indexOf(' ', firstSpace + 1);
  if (secondSpace < 0) {
    return "";
  }

  String target = requestLine.substring(firstSpace + 1, secondSpace);
  const int queryStart = target.indexOf('?');
  if (queryStart < 0) {
    return "";
  }

  String query = target.substring(queryStart + 1);
  int pos = 0;
  while (pos < static_cast<int>(query.length())) {
    int next = query.indexOf('&', pos);
    if (next < 0) {
      next = query.length();
    }

    String part = query.substring(pos, next);
    const int equals = part.indexOf('=');
    if (equals >= 0 && part.substring(0, equals) == "token") {
      return urlDecode(part.substring(equals + 1));
    }
    pos = next + 1;
  }

  return "";
}

String WebApiServer::urlDecode(const String& value) const {
  String out;
  out.reserve(value.length());

  for (size_t i = 0; i < value.length(); ++i) {
    const char c = value[i];
    if (c == '+') {
      out += ' ';
    } else if (c == '%' && i + 2 < value.length()) {
      const char hex[3] = {value[i + 1], value[i + 2], '\0'};
      out += static_cast<char>(strtol(hex, nullptr, 16));
      i += 2;
    } else {
      out += c;
    }
  }

  return out;
}

String WebApiServer::webSocketAcceptKey(const String& key) const {
  const String source = key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
  uint8_t digest[20];
  mbedtls_sha1(reinterpret_cast<const unsigned char*>(source.c_str()), source.length(), digest);

  unsigned char encoded[32];
  size_t encodedLength = 0;
  mbedtls_base64_encode(encoded, sizeof(encoded), &encodedLength, digest, sizeof(digest));
  return String(reinterpret_cast<char*>(encoded)).substring(0, encodedLength);
}

void WebApiServer::sendWebSocketText(WiFiClient& client, const String& payload) {
  if (!client || !client.connected()) {
    return;
  }

  const size_t length = payload.length();
  uint8_t header[4];
  header[0] = 0x81;

  if (length <= 125) {
    header[1] = static_cast<uint8_t>(length);
    client.write(header, 2);
  } else {
    header[1] = 126;
    header[2] = static_cast<uint8_t>((length >> 8) & 0xFF);
    header[3] = static_cast<uint8_t>(length & 0xFF);
    client.write(header, 4);
  }

  client.write(reinterpret_cast<const uint8_t*>(payload.c_str()), length);
}

void WebApiServer::broadcastTtyLogIfChanged() {
  const uint32_t version = cmd_.getLogVersion();
  if (version == lastTtyLogVersion_) {
    return;
  }

  lastTtyLogVersion_ = version;
  String log = cmd_.getLog();
  for (WiFiClient& client : webSocketClients_) {
    sendWebSocketText(client, log);
  }
}

void WebApiServer::handleUpdateUpload(UpdateService::UpdateType type) {
  HTTPUpload& upload = server_.upload();

  if (upload.status == UPLOAD_FILE_START) {
    updateUploadStarted_ = true;
    updateUploadFailed_ = false;
    updateUploadError_ = "";
    updateRollbackPerformed_ = false;
    updateBelongsToBundle_ = false;

    size_t expectedSize = 0;
    if (!parseSizeHeader(server_.header("X-Update-Size"), expectedSize)) {
      updateUploadFailed_ = true;
      updateUploadError_ = "X-Update-Size is invalid";
      return;
    }
    const String expectedSha256 = server_.header("X-Update-SHA256");
    const String bundleId = server_.header("X-Bundle-ID");
    if (!bundleId.isEmpty() && !StorageService::isValidName(bundleId)) {
      updateUploadFailed_ = true;
      updateUploadError_ = "Bundle transaction ID is invalid";
      return;
    }

    if (type == UpdateService::UpdateType::Firmware && !bundleId.isEmpty()) {
      if (!activeBundleId_.isEmpty() && activeBundleId_ != bundleId) {
        updateUploadFailed_ = true;
        updateUploadError_ = "Another bundle transaction is pending";
        return;
      }
      activeBundleId_ = bundleId;
      previousBootPartition_ = esp_ota_get_boot_partition();
      updateBelongsToBundle_ = true;
    } else if (type == UpdateService::UpdateType::Firmware && !activeBundleId_.isEmpty()) {
      updateUploadFailed_ = true;
      updateUploadError_ = "Pending bundle must finish or fail before another firmware update";
      return;
    } else if (type == UpdateService::UpdateType::Filesystem && !bundleId.isEmpty() &&
               (activeBundleId_.isEmpty() || activeBundleId_ != bundleId)) {
      updateUploadFailed_ = true;
      updateUploadError_ = "Bundle transaction ID does not match pending firmware";
      return;
    } else if (type == UpdateService::UpdateType::Filesystem && !bundleId.isEmpty()) {
      updateBelongsToBundle_ = true;
    } else if (type == UpdateService::UpdateType::Filesystem && !activeBundleId_.isEmpty()) {
      updateUploadFailed_ = true;
      updateUploadError_ = "Pending bundle requires its transaction ID";
      return;
    }

    if (!update_.begin(type, expectedSize, expectedSha256)) {
      updateUploadFailed_ = true;
      updateUploadError_ = update_.progress().errorMessage;
      update_.abort(updateUploadError_);
      if (type == UpdateService::UpdateType::Filesystem && updateBelongsToBundle_) {
        rollbackBundle(updateUploadError_);
      } else if (type == UpdateService::UpdateType::Firmware && updateBelongsToBundle_) {
        clearBundleTransaction();
      }
      return;
    }

    cmd_.appendLog(String(type == UpdateService::UpdateType::Firmware ? "firmware" : "filesystem") +
                   " update started: " + expectedSize + " bytes");
    return;
  }

  if (upload.status == UPLOAD_FILE_WRITE) {
    if (updateUploadFailed_) {
      return;
    }
    if (!update_.write(upload.buf, upload.currentSize)) {
      updateUploadFailed_ = true;
      updateUploadError_ = update_.progress().errorMessage;
      update_.abort(updateUploadError_);
      if (type == UpdateService::UpdateType::Filesystem && updateBelongsToBundle_) {
        rollbackBundle(updateUploadError_);
      } else if (type == UpdateService::UpdateType::Firmware && updateBelongsToBundle_) {
        clearBundleTransaction();
      }
    }
    return;
  }

  if (upload.status == UPLOAD_FILE_END) {
    if (updateUploadFailed_) {
      return;
    }
    if (!update_.end()) {
      updateUploadFailed_ = true;
      updateUploadError_ = update_.progress().errorMessage;
      update_.abort(updateUploadError_);
      if (type == UpdateService::UpdateType::Filesystem && updateBelongsToBundle_) {
        rollbackBundle(updateUploadError_);
      } else if (type == UpdateService::UpdateType::Firmware && updateBelongsToBundle_) {
        clearBundleTransaction();
      }
    }
    return;
  }

  if (upload.status == UPLOAD_FILE_ABORTED) {
    updateUploadFailed_ = true;
    updateUploadError_ = "Upload aborted";
    update_.abort(updateUploadError_);
    if (type == UpdateService::UpdateType::Filesystem && updateBelongsToBundle_) {
      rollbackBundle(updateUploadError_);
    } else if (type == UpdateService::UpdateType::Firmware && updateBelongsToBundle_) {
      clearBundleTransaction();
    }
  }
}

void WebApiServer::handleUpdateDone(const char* label) {
  if (!updateUploadStarted_) {
    sendJsonError(400, "update_not_started", "Update upload was not started");
    return;
  }

  updateUploadStarted_ = false;
  if (updateUploadFailed_) {
    cmd_.appendLog(String(label) + " update failed: " + updateUploadError_, "ERROR");
    sendJsonError(500, "update_failed", updateUploadError_.c_str());
    return;
  }

  JsonDocument doc;
  doc["ok"] = true;
  JsonObject data = doc["data"].to<JsonObject>();
  const bool restartAfterUpdate = server_.header("X-Restart-After-Update") != "0";
  const bool filesystemUpdate = String(label) == "Filesystem";
  const bool mustRestart = filesystemUpdate || restartAfterUpdate;
  data["message"] = String(label) + " updated." + (mustRestart ? " Device will restart." : "");
  data["restart"] = mustRestart;
  data["verified"] = true;
  data["sha256"] = update_.progress().sha256;

  String json;
  serializeJson(doc, json);
  server_.send(200, "application/json", json);

  cmd_.appendLog(String(label) + " update completed" + (mustRestart ? ", restarting" : ""));
  if (filesystemUpdate) clearBundleTransaction();
  if (mustRestart) {
    scheduleRestart();
  }
}

bool WebApiServer::rollbackBundle(const String& reason) {
  if (activeBundleId_.isEmpty() || previousBootPartition_ == nullptr) return false;
  const esp_err_t result = esp_ota_set_boot_partition(previousBootPartition_);
  updateRollbackPerformed_ = result == ESP_OK;
  if (updateRollbackPerformed_) {
    updateUploadError_ = reason + "; previous boot partition restored";
    cmd_.appendLog("bundle update rolled back to previous boot partition", "WARN");
  } else {
    updateUploadError_ = reason + "; boot partition rollback failed: " + esp_err_to_name(result);
    cmd_.appendLog(updateUploadError_, "ERROR");
  }
  clearBundleTransaction();
  return updateRollbackPerformed_;
}

void WebApiServer::clearBundleTransaction() {
  activeBundleId_ = "";
  previousBootPartition_ = nullptr;
}

void WebApiServer::scheduleRestart() {
  restartPending_ = true;
  restartAtMs_ = millis() + 1200;
}

void WebApiServer::sendJsonError(int status, const char* code, const char* message) {
  ++apiErrorCount_;
  JsonDocument doc;
  JsonObject error = doc["error"].to<JsonObject>();
  error["code"] = code;
  error["message"] = message;

  String json;
  serializeJson(doc, json);
  server_.send(status, "application/json", json);
}

void WebApiServer::redirectToRoot() {
  server_.sendHeader("Location", "/", true);
  server_.send(302, "text/plain", "");
}

bool WebApiServer::authorizeTtyRequest() {
  String token = server_.arg("token");
  if (token.isEmpty()) {
    token = server_.header("X-TTY-Token");
  }

  if (isTtyTokenValid(token)) {
    return true;
  }

  sendJsonError(401, "tty_unauthorized", "TTY token is invalid");
  return false;
}

void WebApiServer::sendJsonOk() {
  server_.send(200, "application/json", "{\"ok\":true}");
}

void WebApiServer::sendJsonProfile(const String& name, const String& script) {
  JsonDocument doc;
  doc["ok"] = true;
  JsonObject data = doc["data"].to<JsonObject>();
  data["name"] = name;
  data["script"] = script;

  String json;
  serializeJson(doc, json);
  server_.send(200, "application/json", json);
}

void WebApiServer::sendJsonRunStarted(const String& name) {
  JsonDocument doc;
  doc["ok"] = true;
  JsonObject data = doc["data"].to<JsonObject>();
  data["status"] = "started";
  data["profile"] = name;

  String json;
  serializeJson(doc, json);
  server_.send(200, "application/json", json);
}

void WebApiServer::sendJsonTtyOutput(const String& output) {
  JsonDocument doc;
  doc["ok"] = true;
  JsonObject data = doc["data"].to<JsonObject>();
  data["output"] = output;

  String json;
  serializeJson(doc, json);
  server_.send(200, "application/json", json);
}

void WebApiServer::sendJsonTtyLog(const String& log) {
  JsonDocument doc;
  doc["ok"] = true;
  JsonObject data = doc["data"].to<JsonObject>();
  data["log"] = log;

  String json;
  serializeJson(doc, json);
  server_.send(200, "application/json", json);
}

String WebApiServer::statusJson() {
  const DeviceConfig cfg = storage_.loadConfig();
  const WifiState wifi = wifi_.snapshot();
  const MacroStateView macro = macro_.snapshot();
  const BleHidState ble = ble_.snapshot(true);
  const auto profiles = storage_.listProfiles();
  const auto gpioBindings = storage_.loadGpioBindings();

  JsonDocument doc;
  doc["ssid"] = cfg.wifiSsid;
  doc["host"] = cfg.hostname;
  doc["defaultProfile"] = cfg.defaultProfile;
  doc["wifiConnected"] = wifi.connected;
  doc["wifiConnecting"] = wifi.connecting;
  doc["wifiApActive"] = wifi.apActive;
  doc["wifiStatus"] = wifi.status;
  doc["wifiStatusText"] = wifi.statusText;
  doc["wifiPasswordSet"] = wifi.passwordSet;
  doc["wifiSsidFound"] = wifi.ssidFound;
  doc["wifiRssi"] = wifi.rssi;
  doc["wifiChannel"] = wifi.channel;
  doc["ttyAuthRequired"] = !cfg.ttyPassword.isEmpty();
  doc["hidTransport"] = cfg.hidTransport;
  doc["bleMode"] = cfg.bleMode;
  doc["bleName"] = ble.name;
  doc["bleEnabled"] = ble.enabled;
  doc["bleConnected"] = ble.connected;
  doc["blePairing"] = ble.pairing;
  doc["bleBondsAvailable"] = ble.enabled;
  doc["apIp"] = wifi.apIp.toString();
  doc["staIp"] = wifi.staIp.toString();
  doc["busy"] = macro.busy;
  doc["infinite"] = macro.infinite;
  doc["repeatTarget"] = macro.repeatTarget;
  doc["repeatDone"] = macro.repeatDone;
  doc["currentProfile"] = macro.currentProfile;
  doc["message"] = macro.message;

  JsonArray arr = doc["profiles"].to<JsonArray>();
  for (const String& p : profiles) {
    arr.add(p);
  }

  JsonArray gpioArr = doc["gpioBindings"].to<JsonArray>();
  for (const GpioBinding& binding : gpioBindings) {
    JsonObject item = gpioArr.add<JsonObject>();
    item["pin"] = binding.pin;
    item["profile"] = binding.profile;
    item["reserved"] = isGpioStopCommand(binding.profile);
    item["enabled"] = binding.enabled;
    item["mode"] = isGpioStopCommand(binding.profile) ? "stop" : "hold";
  }

  JsonArray bondArr = doc["bleBonds"].to<JsonArray>();
  for (const String& address : ble.bonds) bondArr.add(address);

  String json;
  serializeJson(doc, json);
  return json;
}

String WebApiServer::observabilityJson() {
  const DeviceConfig cfg = storage_.loadConfig();
  const WifiState wifi = wifi_.snapshot();
  const MacroStateView macro = macro_.snapshot();
  const BleHidState ble = ble_.snapshot(true);
  const auto profiles = storage_.listProfiles();
  const auto gpioBindings = storage_.loadGpioBindings();

  JsonDocument doc;
  doc["ok"] = true;
  JsonObject data = doc["data"].to<JsonObject>();
  data["firmwareVersion"] = kFirmwareVersion;
  data["buildDate"] = __DATE__;
  data["buildTime"] = __TIME__;
  data["uptimeMs"] = millis();
  data["chipModel"] = ESP.getChipModel();
  data["chipRevision"] = ESP.getChipRevision();
  data["cpuFreqMhz"] = ESP.getCpuFreqMHz();
  data["sdkVersion"] = ESP.getSdkVersion();
  data["flashSizeBytes"] = ESP.getFlashChipSize();
  data["sketchSizeBytes"] = ESP.getSketchSize();
  data["freeSketchSpaceBytes"] = ESP.getFreeSketchSpace();
  data["heapTotalBytes"] = ESP.getHeapSize();
  data["heapFreeBytes"] = ESP.getFreeHeap();
  data["heapMinFreeBytes"] = ESP.getMinFreeHeap();
  data["heapMaxAllocBytes"] = ESP.getMaxAllocHeap();
  data["filesystemReady"] = storage_.filesystemReady();
  data["filesystemTotalBytes"] = storage_.filesystemReady() ? LittleFS.totalBytes() : 0;
  data["filesystemUsedBytes"] = storage_.filesystemReady() ? LittleFS.usedBytes() : 0;
  data["apiErrors"] = apiErrorCount_;
  data["wifiReconnects"] = wifi_.reconnectAttempts();
  data["macroStarts"] = macro_.startCount();
  data["profileCount"] = profiles.size();
  data["gpioBindingCount"] = gpioBindings.size();
  data["host"] = cfg.hostname;
  data["defaultProfile"] = cfg.defaultProfile;
  data["ttyAuthRequired"] = !cfg.ttyPassword.isEmpty();
  data["wifiConnected"] = wifi.connected;
  data["wifiConnecting"] = wifi.connecting;
  data["wifiApActive"] = wifi.apActive;
  data["wifiStatusText"] = wifi.statusText;
  data["wifiRssi"] = wifi.rssi;
  data["wifiChannel"] = wifi.channel;
  data["apIp"] = wifi.apIp.toString();
  data["staIp"] = wifi.staIp.toString();
  data["hidTransport"] = cfg.hidTransport;
  data["bleMode"] = cfg.bleMode;
  data["bleName"] = ble.name;
  data["bleEnabled"] = ble.enabled;
  data["bleConnected"] = ble.connected;
  data["blePairing"] = ble.pairing;
  data["bleBondCount"] = ble.bonds.size();
  data["macroBusy"] = macro.busy;
  data["currentProfile"] = macro.currentProfile;
  data["ttyLogLevels"] = "INFO,WARN,ERROR";

  String json;
  serializeJson(doc, json);
  return json;
}

void WebApiServer::handleRoot() {
  if (!storage_.filesystemReady()) {
    server_.send(200,
                 "text/html; charset=utf-8",
                 "<!doctype html><html><head><meta charset=\"utf-8\"><title>HIDPad recovery</title>"
                 "<style>body{margin:32px;font-family:sans-serif;background:#111;color:#eee;line-height:1.5}"
                 "code{background:#222;padding:2px 6px;border-radius:4px}</style></head>"
                 "<body><h1>HIDPad web server is running</h1>"
                 "<p>LittleFS is not mounted, so the full web interface cannot be loaded.</p>"
                 "<p>Upload the filesystem image from <code>data/</code> using PlatformIO "
                 "<code>Upload Filesystem Image</code>.</p>"
                 "<p>Diagnostics: <a href=\"/api/ping\">/api/ping</a></p>"
                 "</body></html>");
    return;
  }

  File file = LittleFS.open("/index.html", FILE_READ);
  if (!file) {
    server_.send(200,
                 "text/html; charset=utf-8",
                 "<!doctype html><html><head><meta charset=\"utf-8\"><title>HIDPad</title>"
                 "<style>body{margin:32px;font-family:sans-serif;background:#111;color:#eee}"
                 "code{background:#222;padding:2px 6px;border-radius:4px}</style></head>"
                 "<body><h1>HIDPad web server is running</h1>"
                 "<p><code>/index.html</code> was not found in LittleFS.</p>"
                 "<p>Upload the <code>data/</code> filesystem image to get the full web interface.</p>"
                 "<p>Diagnostics: <a href=\"/api/ping\">/api/ping</a></p>"
                 "</body></html>");
    return;
  }
  server_.streamFile(file, "text/html; charset=utf-8");
  file.close();
}

void WebApiServer::handlePing() {
  server_.send(200, "application/json", "{\"ok\":true,\"data\":{\"pong\":true}}");
}

void WebApiServer::handleStatus() {
  server_.send(200, "application/json", statusJson());
}

void WebApiServer::handleObservability() {
  server_.send(200, "application/json", observabilityJson());
}

void WebApiServer::handleProfileLoad() {
  const String name = server_.arg("name");
  if (!StorageService::isValidName(name)) {
    sendJsonError(400, "invalid_profile_name", "Profile name is required");
    return;
  }
  if (!storage_.profileExists(name)) {
    sendJsonError(404, "profile_not_found", "Profile was not found");
    return;
  }
  sendJsonProfile(name, storage_.loadProfile(name));
}

void WebApiServer::handleProfileSave() {
  const String name = server_.arg("name");
  const String script = server_.arg("script");
  if (!StorageService::isValidName(name)) {
    sendJsonError(400, "invalid_profile_name", "Profile name is required");
    return;
  }
  if (script.length() > AppLimits::kMaxScriptBytes) {
    sendJsonError(413, "script_too_large", "Macro script exceeds 32 KiB");
    return;
  }
  if (!storage_.saveProfile(name, script)) {
    const String error = storage_.lastError();
    cmd_.appendLog(String("profile save failed: ") + error, "ERROR");
    if (error.startsWith("Not enough")) {
      sendJsonError(507, "filesystem_full", error.c_str());
      return;
    }
    sendJsonError(500, "profile_save_failed", "Profile save failed");
    return;
  }

  sendJsonOk();
}

void WebApiServer::handleProfileExport() {
  JsonDocument doc;
  doc["ok"] = true;
  JsonObject data = doc["data"].to<JsonObject>();
  data["version"] = 1;
  JsonArray profiles = data["profiles"].to<JsonArray>();

  for (const String& name : storage_.listProfiles()) {
    JsonObject item = profiles.add<JsonObject>();
    item["name"] = name;
    item["script"] = storage_.loadProfile(name);
  }

  String json;
  serializeJson(doc, json);
  server_.sendHeader("Content-Disposition", "attachment; filename=\"hidpad-profiles.json\"");
  server_.send(200, "application/json", json);
}

void WebApiServer::handleProfileImport() {
  const String body = server_.arg("plain");
  if (body.isEmpty()) {
    sendJsonError(400, "empty_import", "Import payload is empty");
    return;
  }
  if (body.length() > AppLimits::kMaxImportBytes) {
    sendJsonError(413, "import_too_large", "Import payload exceeds 256 KiB");
    return;
  }

  JsonDocument doc;
  if (deserializeJson(doc, body) != DeserializationError::Ok) {
    sendJsonError(400, "invalid_import_json", "Import payload is not valid JSON");
    return;
  }

  JsonArray profiles;
  if (doc["profiles"].is<JsonArray>()) {
    profiles = doc["profiles"].as<JsonArray>();
  } else if (doc["data"]["profiles"].is<JsonArray>()) {
    profiles = doc["data"]["profiles"].as<JsonArray>();
  }

  if (profiles.isNull()) {
    sendJsonError(400, "invalid_import_profiles", "Import payload must contain profiles array");
    return;
  }
  if (profiles.size() > AppLimits::kMaxImportProfiles) {
    sendJsonError(413, "too_many_profiles", "Import contains more than 64 profiles");
    return;
  }

  std::vector<std::pair<String, String>> imported;
  for (JsonObject item : profiles) {
    const String name = String(item["name"] | "");
    const String script = String(item["script"] | "");
    if (!StorageService::isValidName(name)) {
      sendJsonError(400, "invalid_profile_name", "Imported profile name is invalid");
      return;
    }
    if (script.length() > AppLimits::kMaxScriptBytes) {
      sendJsonError(413, "script_too_large", "Imported macro script exceeds 32 KiB");
      return;
    }
    imported.push_back({name, script});
  }

  if (imported.empty()) {
    sendJsonError(400, "empty_import_profiles", "Import payload does not contain profiles");
    return;
  }

  const StorageService::Result result = storage_.importProfilesAtomic(imported);
  if (result != StorageService::Result::Ok) {
    const String error = storage_.lastError();
    cmd_.appendLog(String("profile import failed: ") + error, "ERROR");
    if (result == StorageService::Result::NoSpace) {
      sendJsonError(507, "filesystem_full", error.c_str());
    } else if (result == StorageService::Result::TooLarge) {
      sendJsonError(413, "script_too_large", "Imported macro script exceeds 32 KiB");
    } else if (result == StorageService::Result::Invalid) {
      sendJsonError(400, "invalid_import_profiles", "Import contains invalid or duplicate profiles");
    } else {
      sendJsonError(500, "profile_import_failed", error.c_str());
    }
    return;
  }

  cmd_.appendLog(String("profiles imported: ") + imported.size());
  sendJsonOk();
}

void WebApiServer::handleProfileDelete() {
  const String name = server_.arg("name");
  if (!StorageService::isValidName(name)) {
    sendJsonError(400, "invalid_profile_name", "Profile name is required");
    return;
  }
  if (!storage_.deleteProfile(name)) {
    sendJsonError(500, "profile_delete_failed", "Profile delete failed");
    return;
  }

  std::vector<GpioBinding> bindings = storage_.loadGpioBindings();
  const size_t before = bindings.size();
  bindings.erase(std::remove_if(bindings.begin(),
                                bindings.end(),
                                [&name](const GpioBinding& binding) {
                                  return !isGpioStopCommand(binding.profile) && binding.profile == name;
                                }),
                 bindings.end());
  if (bindings.size() != before) {
    if (!storage_.saveGpioBindings(bindings)) {
      cmd_.appendLog(String("GPIO cleanup save failed: ") + storage_.lastError(), "ERROR");
      sendJsonError(500, "gpio_save_failed", "GPIO bindings save failed");
      return;
    }
    gpio_.applyBindings(storage_.loadGpioBindings());
    cmd_.appendLog(String("gpio bindings removed for profile: ") + name);
  }

  DeviceConfig cfg = storage_.loadConfig();
  if (cfg.defaultProfile == name) {
    cfg.defaultProfile = "";
    if (!storage_.saveConfig(cfg)) {
      cmd_.appendLog(String("default profile clear failed: ") + storage_.lastError(), "ERROR");
      sendJsonError(500, "config_save_failed", "Default profile could not be cleared");
      return;
    }
    cmd_.appendLog(String("default profile cleared: ") + name);
  }

  sendJsonOk();
}

void WebApiServer::handleRun() {
  DeviceConfig cfg = storage_.loadConfig();
  String name = server_.arg("name");
  if (!name.isEmpty() && !StorageService::isValidName(name)) {
    sendJsonError(400, "invalid_profile_name", "Profile name is invalid");
    return;
  }
  if (name.isEmpty()) {
    name = cfg.defaultProfile;
  }
  if (name.isEmpty()) {
    sendJsonError(400, "profile_required", "Profile name is required");
    return;
  }

  if (server_.hasArg("repeat")) {
    sendJsonError(400, "repeat_not_supported", "Use REPEAT or LOOP commands inside the macro script");
    return;
  }

  const String script = storage_.loadProfile(name);
  if (script.isEmpty()) {
    sendJsonError(404, "profile_not_found", "Profile was not found or has an empty script");
    return;
  }
  if (!macro_.start(name, script)) {
    sendJsonError(409, "macro_busy", "Macro engine is busy");
    return;
  }

  cmd_.appendLog(String("run profile: ") + name);
  sendJsonRunStarted(name);
}

void WebApiServer::handleRunScript() {
  String name = server_.arg("name");
  if (!name.isEmpty() && !StorageService::isValidName(name)) {
    sendJsonError(400, "invalid_profile_name", "Profile name is invalid");
    return;
  }
  if (name.isEmpty()) {
    name = "draft";
  }

  if (server_.hasArg("repeat")) {
    sendJsonError(400, "repeat_not_supported", "Use REPEAT or LOOP commands inside the macro script");
    return;
  }

  const String script = server_.arg("script");
  if (script.isEmpty()) {
    sendJsonError(400, "empty_script", "Macro script is empty");
    return;
  }
  if (script.length() > AppLimits::kMaxScriptBytes) {
    sendJsonError(413, "script_too_large", "Macro script exceeds 32 KiB");
    return;
  }
  if (!macro_.start(name, script)) {
    sendJsonError(409, "macro_busy", "Macro engine is busy");
    return;
  }

  cmd_.appendLog(String("run draft: ") + name);
  sendJsonRunStarted(name);
}

void WebApiServer::handleStop() {
  if (!macro_.stop()) {
    sendJsonError(409, "macro_not_running", "Macro engine is not running");
    return;
  }

  cmd_.appendLog("macro stopped");
  sendJsonOk();
}

void WebApiServer::handleWifiSave() {
  DeviceConfig cfg = storage_.loadConfig();
  const String oldHostname = cfg.hostname;
  const String oldWifiSsid = cfg.wifiSsid;
  const String oldWifiPassword = cfg.wifiPassword;

  const String hostname = server_.arg("hostname");
  if (!hostname.isEmpty() && !StorageService::isValidName(hostname)) {
    sendJsonError(400, "invalid_hostname", "Hostname is invalid");
    return;
  }
  cfg.hostname = hostname;
  if (cfg.hostname.isEmpty()) {
    cfg.hostname = "hidpad-s3";
  }
  cfg.wifiSsid = server_.arg("ssid");
  cfg.wifiSsid.trim();
  if (server_.hasArg("pass")) {
    cfg.wifiPassword = server_.arg("pass");
  }
  const String defaultProfile = server_.arg("defaultProfile");
  if (!defaultProfile.isEmpty() && !StorageService::isValidName(defaultProfile)) {
    sendJsonError(400, "invalid_profile_name", "Default profile name is invalid");
    return;
  }
  cfg.defaultProfile = defaultProfile;
  if (server_.hasArg("ttyPass")) {
    cfg.ttyPassword = server_.arg("ttyPass");
  }

  if (!storage_.saveConfig(cfg)) {
    cmd_.appendLog(String("config save failed: ") + storage_.lastError(), "ERROR");
    sendJsonError(500, "config_save_failed", "Configuration save failed");
    return;
  }

  const bool wifiChanged =
      oldHostname != cfg.hostname || oldWifiSsid != cfg.wifiSsid || oldWifiPassword != cfg.wifiPassword;
  if (wifiChanged) {
    wifi_.applyConfig(cfg);
    cmd_.appendLog("wifi config updated");
  } else {
    cmd_.appendLog("device config updated");
  }
  sendJsonOk();
}

void WebApiServer::handleWifiReset() {
  DeviceConfig cfg = storage_.loadConfig();
  cfg.wifiSsid = "";
  cfg.wifiPassword = "";

  if (!storage_.saveConfig(cfg)) {
    cmd_.appendLog(String("config save failed: ") + storage_.lastError(), "ERROR");
    sendJsonError(500, "config_save_failed", "Configuration save failed");
    return;
  }

  cmd_.appendLog("wifi credentials reset");
  sendJsonOk();
  wifi_.applyConfig(cfg);
}

void WebApiServer::handleBleSave() {
  DeviceConfig cfg = storage_.loadConfig();
  const String transport = server_.arg("transport");
  const String mode = server_.arg("mode");

  if (transport != "usb" && transport != "ble") {
    sendJsonError(400, "invalid_hid_transport", "HID transport must be usb or ble");
    return;
  }
  if (mode != "keyboard" && mode != "mouse") {
    sendJsonError(400, "invalid_ble_mode", "BLE mode must be keyboard or mouse");
    return;
  }
  cfg.hidTransport = transport;
  cfg.bleMode = mode;
  if (!storage_.saveConfig(cfg)) {
    cmd_.appendLog(String("BLE config save failed: ") + storage_.lastError(), "ERROR");
    sendJsonError(500, "config_save_failed", "BLE configuration save failed");
    return;
  }

  cmd_.appendLog(String("HID output updated: ") + transport + "/" + mode);
  sendJsonOk();
  scheduleRestart();
}

void WebApiServer::handleBleBondDelete() {
  String address = server_.arg("address");
  address.trim();
  if (address.length() != 17) {
    sendJsonError(400, "invalid_ble_address", "BLE bond address is invalid");
    return;
  }
  if (!ble_.snapshot().enabled) {
    sendJsonError(409, "ble_not_enabled", "Enable the BLE transport before managing bonds");
    return;
  }
  if (!ble_.removeBond(address)) {
    sendJsonError(500, "ble_bond_delete_failed", "BLE bond could not be removed");
    return;
  }
  cmd_.appendLog(String("BLE bond removed: ") + address);
  sendJsonOk();
}

void WebApiServer::handleBleBondsClear() {
  if (!ble_.snapshot().enabled) {
    sendJsonError(409, "ble_not_enabled", "Enable the BLE transport before managing bonds");
    return;
  }
  if (!ble_.clearBonds()) {
    sendJsonError(500, "ble_bonds_clear_failed", "BLE bonds could not be cleared");
    return;
  }
  cmd_.appendLog("all BLE bonds cleared");
  sendJsonOk();
}

void WebApiServer::handleGpioSave() {
  const String pinArg = server_.arg("pin");
  char* end = nullptr;
  const long parsedPin = strtol(pinArg.c_str(), &end, 10);
  if (end == pinArg.c_str() || *end != '\0' || parsedPin < 0 || parsedPin > 255) {
    sendJsonError(400, "invalid_gpio_pin", "GPIO pin is invalid");
    return;
  }

  const uint8_t pin = static_cast<uint8_t>(parsedPin);
  if (!GpioService::isPinAllowed(pin)) {
    sendJsonError(400, "gpio_pin_not_allowed", "GPIO pin is not allowed for button binding");
    return;
  }

  const String rawProfile = server_.arg("profile");
  if (!isGpioStopCommand(rawProfile) && !StorageService::isValidName(rawProfile)) {
    sendJsonError(400, "invalid_profile_name", "Existing profile name is required");
    return;
  }
  const String profile = isGpioStopCommand(rawProfile) ? String(kGpioStopCommand) : rawProfile;
  if (profile.isEmpty() || (!isGpioStopCommand(profile) && !storage_.profileExists(profile))) {
    sendJsonError(400, "invalid_profile_name", "Existing profile name is required");
    return;
  }

  std::vector<GpioBinding> bindings = storage_.loadGpioBindings();
  if (isGpioStopCommand(profile)) {
    bindings.erase(std::remove_if(bindings.begin(),
                                  bindings.end(),
                                  [](const GpioBinding& binding) {
                                    return isGpioStopCommand(binding.profile);
                                  }),
                   bindings.end());
  }
  bool replaced = false;
  for (GpioBinding& binding : bindings) {
    if (binding.pin == pin) {
      binding.profile = profile;
      binding.enabled = true;
      replaced = true;
      break;
    }
  }
  if (!replaced) {
    GpioBinding binding;
    binding.pin = pin;
    binding.profile = profile;
    binding.enabled = true;
    bindings.push_back(binding);
  }

  if (!storage_.saveGpioBindings(bindings)) {
    cmd_.appendLog(String("GPIO save failed: ") + storage_.lastError(), "ERROR");
    sendJsonError(500, "gpio_save_failed", "GPIO bindings save failed");
    return;
  }

  gpio_.applyBindings(storage_.loadGpioBindings());
  cmd_.appendLog(String("gpio binding saved: GPIO") + pin + " -> " + profile);
  sendJsonOk();
}

void WebApiServer::handleGpioDelete() {
  const String pinArg = server_.arg("pin");
  char* end = nullptr;
  const long parsedPin = strtol(pinArg.c_str(), &end, 10);
  if (end == pinArg.c_str() || *end != '\0' || parsedPin < 0 || parsedPin > 255) {
    sendJsonError(400, "invalid_gpio_pin", "GPIO pin is invalid");
    return;
  }

  const uint8_t pin = static_cast<uint8_t>(parsedPin);
  std::vector<GpioBinding> bindings = storage_.loadGpioBindings();
  const auto reserved = std::find_if(bindings.begin(),
                                     bindings.end(),
                                     [pin](const GpioBinding& binding) {
                                       return binding.pin == pin && isGpioStopCommand(binding.profile);
                                     });
  if (reserved != bindings.end()) {
    sendJsonError(400, "gpio_stop_reserved", "GPIO STOP binding cannot be deleted");
    return;
  }

  bindings.erase(std::remove_if(bindings.begin(),
                                bindings.end(),
                                [pin](const GpioBinding& binding) { return binding.pin == pin; }),
                 bindings.end());

  if (!storage_.saveGpioBindings(bindings)) {
    cmd_.appendLog(String("GPIO delete failed: ") + storage_.lastError(), "ERROR");
    sendJsonError(500, "gpio_delete_failed", "GPIO binding delete failed");
    return;
  }

  gpio_.applyBindings(storage_.loadGpioBindings());
  cmd_.appendLog(String("gpio binding deleted: GPIO") + pin);
  sendJsonOk();
}

void WebApiServer::handleTtyExec() {
  if (!authorizeTtyRequest()) {
    return;
  }

  const String cmd = server_.arg("cmd");
  const String out = cmd_.execute(cmd);
  sendJsonTtyOutput(out);
}

void WebApiServer::handleTtyLog() {
  if (!authorizeTtyRequest()) {
    return;
  }

  sendJsonTtyLog(cmd_.getLog());
}

void WebApiServer::handleUpdateProgress() {
  const auto progress = update_.progress();

  JsonDocument doc;
  doc["ok"] = true;
  JsonObject data = doc["data"].to<JsonObject>();
  data["inProgress"] = progress.inProgress;
  data["completed"] = progress.completed;
  data["type"] = progress.type == UpdateService::UpdateType::Firmware ? "firmware" : "filesystem";
  data["totalSize"] = static_cast<uint32_t>(progress.totalSize);
  data["writtenSize"] = static_cast<uint32_t>(progress.writtenSize);
  data["stage"] = progress.stage;
  data["sha256"] = progress.sha256;
  data["percentage"] = progress.totalSize > 0
                           ? static_cast<uint8_t>((progress.writtenSize * 100) / progress.totalSize)
                           : 0;
  if (!progress.errorMessage.isEmpty()) {
    data["error"] = progress.errorMessage;
  }

  String json;
  serializeJson(doc, json);
  server_.send(200, "application/json", json);
}

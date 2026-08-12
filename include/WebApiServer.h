#pragma once

#include <WebServer.h>
#include <WiFi.h>

#include "AppConfig.h"
#include "BleHidService.h"
#include "CommandService.h"
#include "GpioService.h"
#include "MacroEngine.h"
#include "StorageService.h"
#include "UpdateService.h"
#include "WifiService.h"

class WebApiServer {
 public:
  WebApiServer(StorageService& storage,
               WifiService& wifi,
               MacroEngine& macro,
               BleHidService& ble,
               CommandService& cmd,
               GpioService& gpio,
               UpdateService& update);

  void init();
  void handleClient();

 private:
  void registerRoutes();
  void registerWebSocket();

  void streamFile(const char* path, const char* mimeType);
  void handleWebSocketClients();
  void acceptWebSocketClient(WiFiClient client);
  bool performWebSocketHandshake(WiFiClient& client);
  bool isTtyTokenValid(const String& token);
  String tokenFromRequestLine(const String& requestLine) const;
  String urlDecode(const String& value) const;
  String webSocketAcceptKey(const String& key) const;
  void sendWebSocketText(WiFiClient& client, const String& payload);
  void broadcastTtyLogIfChanged();
  bool authorizeTtyRequest();
  void handleUpdateUpload(UpdateService::UpdateType type);
  void handleUpdateDone(const char* label);
  void scheduleRestart();
  void sendJsonOk();
  void sendJsonProfile(const String& name, const String& script);
  void sendJsonRunStarted(const String& name);
  void sendJsonTtyOutput(const String& output);
  void sendJsonTtyLog(const String& log);
  void sendJsonError(int status, const char* code, const char* message);
  void redirectToRoot();
  String statusJson();
  String observabilityJson();

  void handleRoot();
  void handlePing();
  void handleStatus();
  void handleObservability();
  void handleProfileLoad();
  void handleProfileSave();
  void handleProfileExport();
  void handleProfileImport();
  void handleProfileDelete();
  void handleRun();
  void handleRunScript();
  void handleStop();
  void handleWifiSave();
  void handleWifiReset();
  void handleBleSave();
  void handleBleBondDelete();
  void handleBleBondsClear();
  void handleGpioSave();
  void handleGpioDelete();
  void handleTtyExec();
  void handleTtyLog();
  void handleUpdateProgress();

  StorageService& storage_;
  WifiService& wifi_;
  MacroEngine& macro_;
  BleHidService& ble_;
  CommandService& cmd_;
  GpioService& gpio_;
  UpdateService& update_;
  WebServer server_;
  WiFiServer webSocketServer_;
  WiFiClient webSocketClients_[4];
  uint32_t lastTtyLogVersion_ = 0;
  uint32_t apiErrorCount_ = 0;
  bool updateUploadStarted_ = false;
  bool updateUploadFailed_ = false;
  String updateUploadError_;
  bool restartPending_ = false;
  uint32_t restartAtMs_ = 0;
};

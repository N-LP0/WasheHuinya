#include "WifiService.h"

namespace {
constexpr char kApSsid[] = "HIDPad-Setup";
constexpr char kApPassword[] = "hidpad1234";
constexpr uint16_t kDnsPort = 53;
constexpr uint32_t kStaReconnectIntervalMs = 15000;
constexpr bool kWifiPowerSaveEnabled = true;
constexpr wifi_power_t kWifiTxPower = WIFI_POWER_13dBm;

String wifiStatusText(wl_status_t status) {
  switch (status) {
    case WL_IDLE_STATUS:
      return "idle";
    case WL_NO_SSID_AVAIL:
      return "ssid_not_available";
    case WL_SCAN_COMPLETED:
      return "scan_completed";
    case WL_CONNECTED:
      return "connected";
    case WL_CONNECT_FAILED:
      return "connect_failed";
    case WL_CONNECTION_LOST:
      return "connection_lost";
    case WL_DISCONNECTED:
      return "disconnected";
    default:
      return String("unknown_") + static_cast<int>(status);
  }
}
}  // namespace

void WifiService::init(const DeviceConfig& config) {
  mutex_ = xSemaphoreCreateMutex();
  config_ = config;

  WiFi.persistent(false);
  if (config_.wifiSsid.isEmpty()) {
    startAccessPoint();
  } else {
    startStationIfConfigured();
    startAccessPoint();
  }
}

void WifiService::applyConfig(const DeviceConfig& config) {
  xSemaphoreTake(mutex_, portMAX_DELAY);
  config_ = config;
  xSemaphoreGive(mutex_);

  if (config.wifiSsid.isEmpty()) {
    WiFi.disconnect(false, false);
    startAccessPoint();
  } else {
    startStationIfConfigured();
    if (WiFi.status() != WL_CONNECTED) {
      startAccessPoint();
    }
  }
}

void WifiService::tick() {
  xSemaphoreTake(mutex_, portMAX_DELAY);
  const DeviceConfig cfg = config_;
  const bool apActive = apActive_;
  xSemaphoreGive(mutex_);

  if (apActive) {
    dnsServer_.processNextRequest();
  }

  if (WiFi.status() == WL_CONNECTED) {
    xSemaphoreTake(mutex_, portMAX_DELAY);
    lastSsidFound_ = true;
    lastRssi_ = WiFi.RSSI();
    lastChannel_ = WiFi.channel();
    xSemaphoreGive(mutex_);
    if (apActive) {
      stopAccessPoint();
    }
    return;
  }

  if (!apActive) {
    startAccessPoint();
  }

  const bool staConfigured = !cfg.wifiSsid.isEmpty();
  const uint32_t now = millis();
  if (staConfigured && now - lastStaAttemptMs_ >= kStaReconnectIntervalMs) {
    startStationIfConfigured();
  }
}

WifiState WifiService::snapshot() const {
  WifiState state;
  const wl_status_t status = WiFi.status();
  state.status = static_cast<uint8_t>(status);
  state.statusText = wifiStatusText(status);
  state.connected = (status == WL_CONNECTED);
  state.apIp = WiFi.softAPIP();
  state.staIp = WiFi.localIP();

  xSemaphoreTake(mutex_, portMAX_DELAY);
  state.apActive = apActive_;
  state.connecting = !state.connected && !config_.wifiSsid.isEmpty();
  state.passwordSet = !config_.wifiPassword.isEmpty();
  state.ssidFound = lastSsidFound_;
  state.rssi = lastRssi_;
  state.channel = lastChannel_;
  xSemaphoreGive(mutex_);

  return state;
}

uint32_t WifiService::reconnectAttempts() const {
  xSemaphoreTake(mutex_, portMAX_DELAY);
  const uint32_t attempts = reconnectAttempts_;
  xSemaphoreGive(mutex_);
  return attempts;
}

String WifiService::normalizedHostname(const DeviceConfig& config) const {
  String hostname = config.hostname;
  hostname.trim();
  if (hostname.isEmpty()) {
    hostname = "hidpad-s3";
  }
  return hostname;
}

void WifiService::startAccessPoint() {
  xSemaphoreTake(mutex_, portMAX_DELAY);
  const DeviceConfig cfg = config_;
  const bool alreadyActive = apActive_;
  xSemaphoreGive(mutex_);

  if (alreadyActive && WiFi.softAPIP() != IPAddress(0, 0, 0, 0)) {
    return;
  }

  const IPAddress apIp(192, 168, 4, 1);
  const IPAddress gateway(192, 168, 4, 1);
  const IPAddress subnet(255, 255, 255, 0);
  const String hostname = normalizedHostname(cfg);

  WiFi.setHostname(hostname.c_str());
  WiFi.mode(WIFI_AP_STA);
  WiFi.setSleep(kWifiPowerSaveEnabled);
  WiFi.setTxPower(kWifiTxPower);
  WiFi.softAPConfig(apIp, gateway, subnet);
  WiFi.softAPsetHostname(hostname.c_str());
  const bool started = WiFi.softAP(kApSsid, kApPassword, 1, false, 4);
  if (started) {
    dnsServer_.stop();
    dnsServer_.setErrorReplyCode(DNSReplyCode::NoError);
    dnsServer_.start(kDnsPort, "*", apIp);
  }
  xSemaphoreTake(mutex_, portMAX_DELAY);
  apActive_ = started;
  xSemaphoreGive(mutex_);
  Serial.printf("WiFi AP %s: ssid=%s ip=%s\n",
                started ? "started" : "failed",
                kApSsid,
                WiFi.softAPIP().toString().c_str());
}

void WifiService::stopAccessPoint() {
  xSemaphoreTake(mutex_, portMAX_DELAY);
  const bool apActive = apActive_;
  xSemaphoreGive(mutex_);

  if (!apActive) {
    return;
  }

  dnsServer_.stop();
  WiFi.softAPdisconnect(true);
  if (WiFi.status() == WL_CONNECTED) {
    WiFi.mode(WIFI_STA);
  }

  xSemaphoreTake(mutex_, portMAX_DELAY);
  apActive_ = false;
  xSemaphoreGive(mutex_);
  Serial.println("WiFi AP stopped after STA connection");
}

void WifiService::startStationIfConfigured() {
  xSemaphoreTake(mutex_, portMAX_DELAY);
  const DeviceConfig cfg = config_;
  xSemaphoreGive(mutex_);

  if (cfg.wifiSsid.isEmpty()) {
    return;
  }

  const String hostname = normalizedHostname(cfg);
  WiFi.setHostname(hostname.c_str());
  WiFi.mode(WIFI_AP_STA);
  WiFi.setSleep(kWifiPowerSaveEnabled);
  WiFi.setTxPower(kWifiTxPower);
  WiFi.disconnect(false, false);
  WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE);
  WiFi.setHostname(hostname.c_str());

  xSemaphoreTake(mutex_, portMAX_DELAY);
  lastSsidFound_ = false;
  lastRssi_ = 0;
  lastChannel_ = 0;
  ++reconnectAttempts_;
  xSemaphoreGive(mutex_);

  // WiFi.begin starts the driver's own asynchronous connection procedure. An explicit
  // synchronous scan here disrupts AP traffic and can stall WebUI asset downloads.
  WiFi.begin(cfg.wifiSsid.c_str(), cfg.wifiPassword.c_str());

  Serial.printf("WiFi STA connect: ssid=%s password=%s\n",
                cfg.wifiSsid.c_str(),
                cfg.wifiPassword.isEmpty() ? "empty" : "set");
  lastStaAttemptMs_ = millis();
}

#include "network.h"

#include <WiFi.h>

#include "serial_log.h"

#if __has_include("secrets.h")
#include "secrets.h"
#endif
#ifndef CUELINK_WIFI_SSID
#define CUELINK_WIFI_SSID ""
#endif
#ifndef CUELINK_WIFI_PASSWORD
#define CUELINK_WIFI_PASSWORD ""
#endif

const char* Network::ssid() const { return settings_.ssid.c_str(); }
NetworkSettings Network::developmentSettings() const {
  NetworkSettings settings;
  settings.ssid = CUELINK_WIFI_SSID;
  settings.password = CUELINK_WIFI_PASSWORD;
  return settings;
}
void Network::configure(const NetworkSettings& settings, uint32_t now) {
  settings_ = settings;
  settingsProvided_ = true;
  configured_ = !settings_.ssid.empty();
  if (initialized_) WiFi.disconnect(false, false);
  attempting_ = false;
  clearAddressing();
  nextBackoff_ = 2000;
  if (!configured_) {
    setState(State::NotConfigured);
    return;
  }
  if (!initialized_) {
    begin(now);
    return;
  }
  setState(State::Connecting);
  startAttempt(now);
}

const char* Network::stateText() const {
  switch (state_) {
    case State::NotConfigured:
      return "Not configured";
    case State::Connecting:
      return "Connecting";
    case State::Connected:
      return "Connected";
    case State::Reconnecting:
      return "Reconnecting";
  }
  return "Unavailable";
}

void Network::setState(State state) {
  if (state_ == state) return;
  state_ = state;
  FirmwareSerial.printf("[Wi-Fi] %s\n", stateText());
}

void Network::begin(uint32_t now) {
  if (!settingsProvided_) settings_ = developmentSettings();
  configured_ = !settings_.ssid.empty();
  if (!configured_) {
    FirmwareSerial.println("[Wi-Fi] Wi-Fi not configured");
    return;  // Do not initialize the radio or use any previously stored AP.
  }
  WiFi.persistent(false);                  // Set RAM storage before enabling the radio.
  WiFi.setAutoReconnect(false);            // This class owns the retry schedule.
  WiFi.setHostname("cuelink-controller");  // Before starting station mode.
  WiFi.onEvent([this](WiFiEvent_t event, WiFiEventInfo_t info) {
    if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
      portENTER_CRITICAL(&eventMux_);
      ++disconnectEvents_;
      disconnectReason_ = info.wifi_sta_disconnected.reason;
      portEXIT_CRITICAL(&eventMux_);
    } else if (event == ARDUINO_EVENT_WIFI_STA_LOST_IP) {
      portENTER_CRITICAL(&eventMux_);
      lostIp_ = true;
      portEXIT_CRITICAL(&eventMux_);
    }
  });
  setState(State::Connecting);
  startAttempt(now);
}

void Network::startAttempt(uint32_t now) {
  attempting_ = true;
  attemptAt_ = now;
  FirmwareSerial.println("[Wi-Fi] Starting station/DHCP attempt (15s maximum)");
  if (!initialized_) {
    // A new firmware boot uses DHCP by default; no static config is installed.
    initialized_ = WiFi.mode(WIFI_STA);
  }
#ifdef ARDUINO
  auto address = [](const std::string& text) {
    IPAddress ip;
    ip.fromString(text.c_str());
    return ip;
  };
  const bool addressing = settings_.dhcp
                              ? WiFi.config(IPAddress(), IPAddress(), IPAddress())
                              : WiFi.config(address(settings_.ip), address(settings_.gateway),
                                            address(settings_.subnet), address(settings_.dns1),
                                            address(settings_.dns2));
  if (!addressing) {
    FirmwareSerial.println("[Wi-Fi] Address configuration failed");
    retryLater(now);
    return;
  }
#endif
  if (!initialized_ ||
      WiFi.begin(settings_.ssid.c_str(), settings_.password.c_str()) == WL_CONNECT_FAILED) {
    FirmwareSerial.println("[Wi-Fi] Could not start connection");
    retryLater(now);
  }
}

void Network::clearAddressing() {
  ip_ = subnet_ = gateway_ = dns_ = IPAddress();
  rssi_ = 0;
  channel_ = 0;
}

void Network::retryLater(uint32_t now) {
  // Cancel association/DHCP before scheduling another attempt. Do not erase AP
  // settings or switch off STA; credentials remain in RAM only.
  WiFi.disconnect(false, false);
  attempting_ = false;
  clearAddressing();
  setState(State::Reconnecting);
  retryAt_ = now;
  retryDelay_ = nextBackoff_;
  nextBackoff_ = nextBackoff_ >= 16000 ? 30000 : nextBackoff_ * 2;
  FirmwareSerial.printf("[Wi-Fi] Retry in %lu ms\n", static_cast<unsigned long>(retryDelay_));
}

void Network::refresh() {
  ip_ = WiFi.localIP();
  subnet_ = WiFi.subnetMask();
  gateway_ = WiFi.gatewayIP();
  dns_ = WiFi.dnsIP(0);
  rssi_ = WiFi.RSSI();
  channel_ = WiFi.channel();
}

void Network::update(uint32_t now) {
  if (!configured_) return;
  portENTER_CRITICAL(&eventMux_);
  const uint32_t disconnects = disconnectEvents_;
  const uint8_t reason = disconnectReason_;
  const bool lostIp = lostIp_;
  disconnectEvents_ = 0;
  lostIp_ = false;
  portEXIT_CRITICAL(&eventMux_);
  if (disconnects) {
    FirmwareSerial.printf(
        "[Wi-Fi] Disconnect reason=%u (%s); events=%lu\n", reason,
        WiFi.disconnectReasonName(static_cast<wifi_err_reason_t>(reason ? reason : 1)),
        static_cast<unsigned long>(disconnects));
  }
  if (lostIp) FirmwareSerial.println("[Wi-Fi] IPv4 lease lost");

  bool ready = WiFi.status() == WL_CONNECTED && uint32_t(WiFi.localIP()) != 0;
#ifdef ARDUINO
  ready = ready && WiFi.SSID() == settings_.ssid.c_str();
  if (ready && !settings_.dhcp) {
    IPAddress expected;
    expected.fromString(settings_.ip.c_str());
    ready = WiFi.localIP() == expected;
  }
#endif
  if (connected() && (disconnects || lostIp || !ready)) {
    ++losses_;  // Once per loss of an observed successful connection.
    retryLater(now);
    return;
  }
  if (attempting_ && ready) {
    attempting_ = false;
    nextBackoff_ = 2000;
    refresh();
    sampledAt_ = now;
    setState(State::Connected);
    logStatus();
  } else if (connected()) {
    if (uint32_t(now - sampledAt_) >= 1000) {
      const IPAddress previous = ip_;
      refresh();
      sampledAt_ = now;
      if (previous != ip_) logStatus();
    }
  } else if (attempting_) {
    // Arduino-ESP32 2.x may perform one internal first-connect retry even with
    // auto reconnect disabled. It stays inside this single bounded attempt.
    if (uint32_t(now - attemptAt_) >= attemptMs) {
      FirmwareSerial.println("[Wi-Fi] Connection/DHCP attempt timed out");
      retryLater(now);
    }
  } else if (uint32_t(now - retryAt_) >= retryDelay_) {
    startAttempt(now);
  }
}

void Network::logStatus() const {
  if (!configured_) {
    FirmwareSerial.println("[Wi-Fi] Wi-Fi not configured");
    return;
  }
  FirmwareSerial.printf("[Wi-Fi] %s; hostname=cuelink-controller; losses=%lu\n", stateText(),
                        static_cast<unsigned long>(losses_));
  if (!connected()) return;
  FirmwareSerial.printf("[Wi-Fi] IPv4=%s mask=%s gateway=%s DNS=%s; RSSI=%ld dBm channel=%u\n",
                        ip_.toString().c_str(), subnet_.toString().c_str(),
                        gateway_.toString().c_str(), dns_.toString().c_str(),
                        static_cast<long>(rssi_), channel_);
}

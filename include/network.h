#pragma once

#include <Arduino.h>
#include "network_settings.h"
#include <IPAddress.h>
#include <freertos/FreeRTOS.h>

class Network {
 public:
  enum class State { NotConfigured, Connecting, Connected, Reconnecting };
  void begin(uint32_t now);
  void update(uint32_t now);
  NetworkSettings developmentSettings() const;
  void configure(const NetworkSettings& settings, uint32_t now);
  const NetworkSettings& settings() const { return settings_; }
  void logStatus() const;
  State state() const { return state_; }
  const char* stateText() const;
  const char* ssid() const;
  bool connected() const { return state_ == State::Connected; }
  IPAddress ip() const { return ip_; }
  IPAddress subnet() const { return subnet_; }
  IPAddress gateway() const { return gateway_; }
  IPAddress dns() const { return dns_; }
  int32_t rssi() const { return rssi_; }
  uint8_t channel() const { return channel_; }
  uint32_t losses() const { return losses_; }

 private:
  NetworkSettings settings_;
  bool settingsProvided_ = false;
  void startAttempt(uint32_t now);
  void retryLater(uint32_t now);
  void clearAddressing();
  void refresh();
  void setState(State state);
  static constexpr uint32_t attemptMs = 15000;
  State state_ = State::NotConfigured;
  bool configured_ = false;
  bool initialized_ = false;
  bool attempting_ = false;
  uint32_t attemptAt_ = 0;
  uint32_t retryAt_ = 0;
  uint32_t retryDelay_ = 0;
  uint32_t nextBackoff_ = 2000;
  uint32_t sampledAt_ = 0;
  uint32_t losses_ = 0;
  IPAddress ip_, subnet_, gateway_, dns_;
  int32_t rssi_ = 0;
  uint8_t channel_ = 0;

  // The Wi-Fi task only writes this mailbox under the spinlock. All state,
  // Serial output, Wi-Fi control, and rendering belong to the main loop.
  portMUX_TYPE eventMux_ = portMUX_INITIALIZER_UNLOCKED;
  uint32_t disconnectEvents_ = 0;
  uint8_t disconnectReason_ = 0;
  bool lostIp_ = false;
};

#pragma once
#include "Arduino.h"
#include "IPAddress.h"
#include <cassert>
#include <functional>
enum WiFiEvent_t { ARDUINO_EVENT_WIFI_STA_DISCONNECTED, ARDUINO_EVENT_WIFI_STA_LOST_IP };
struct WiFiEventInfo_t { struct { uint8_t reason = 0; } wifi_sta_disconnected; };
enum wifi_err_reason_t { WIFI_REASON_UNSPECIFIED = 1 };
enum { WIFI_STA, WL_DISCONNECTED, WL_CONNECTED, WL_CONNECT_FAILED };
struct WiFiStub {
  std::function<void(WiFiEvent_t, WiFiEventInfo_t)> callback;
  unsigned attempts = 0, cancellations = 0;
  bool ramOnly = false, autoReconnect = true, station = false, accept = true;
  int statusValue = WL_DISCONNECTED;
  IPAddress address;
  void persistent(bool value) { ramOnly = !value; }
  void setAutoReconnect(bool value) { autoReconnect = value; }
  void setHostname(const char* value) { assert(std::string(value) == "cuelink-controller"); }
  void onEvent(std::function<void(WiFiEvent_t, WiFiEventInfo_t)> cb) { callback = cb; }
  bool mode(int value) { assert(value == WIFI_STA && ramOnly); station = true; return true; }
  int begin(const char*, const char*) {
    assert(station && ramOnly && !autoReconnect);
    ++attempts;
    return accept ? WL_DISCONNECTED : WL_CONNECT_FAILED;
  }
  void disconnect(bool off, bool erase) {
    assert(!off && !erase);
    ++cancellations;
    statusValue = WL_DISCONNECTED;
    // Deliberately leave old addresses in the driver to test stale-data handling.
    emit(ARDUINO_EVENT_WIFI_STA_DISCONNECTED, 8);
  }
  int status() const { return statusValue; }
  IPAddress localIP() const { return address; }
  IPAddress subnetMask() const { return IPAddress(2); }
  IPAddress gatewayIP() const { return IPAddress(3); }
  IPAddress dnsIP(int) const { return IPAddress(4); }
  int32_t RSSI() const { return -60; }
  uint8_t channel() const { return 6; }
  const char* disconnectReasonName(wifi_err_reason_t) const { return "test reason"; }
  void emit(WiFiEvent_t event, uint8_t reason = 0) {
    WiFiEventInfo_t info;
    info.wifi_sta_disconnected.reason = reason;
    callback(event, info);
  }
};
extern WiFiStub WiFi;

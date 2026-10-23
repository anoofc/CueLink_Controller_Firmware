#pragma once
#include <NimBLEDevice.h>
#include "ble_protocol.h"
#include "buttons.h"
#include "network.h"

class AppConnection {
 public:
  void begin(const Network& network, Mode mode, uint32_t now);
  void update(const Network& network, Mode mode, bool discoveryRequested, uint32_t now);
  const char* stateText() const;
  const char* name() const { return name_; }
  uint32_t remainingSeconds(uint32_t now) const { return window_.remainingSeconds(now); }
 private:
  class ServerEvents : public NimBLEServerCallbacks {
   public:
    explicit ServerEvents(AppConnection& owner) : owner_(owner) {}
    void onConnect(NimBLEServer*, NimBLEConnInfo&) override;
    void onDisconnect(NimBLEServer*, NimBLEConnInfo&, int reason) override;
   private:
    AppConnection& owner_;
  } serverEvents_{*this};
  class StatusEvents : public NimBLECharacteristicCallbacks {
   public:
    explicit StatusEvents(AppConnection& owner) : owner_(owner) {}
    void onSubscribe(NimBLECharacteristic*, NimBLEConnInfo&, uint16_t value) override;
   private:
    AppConnection& owner_;
  } statusEvents_{*this};
  void refresh(const Network& network, Mode mode);
  NimBLECharacteristic* status_ = nullptr;
  NimBLEAdvertising* advertising_ = nullptr;
  CueLinkBle::DiscoveryWindow window_;
  char name_[13] = {};
  bool available_ = false;
  bool connected_ = false;
  bool advertisingFailed_ = false;
  uint32_t sampledAt_ = 0;
  uint32_t notifiedAt_ = 0;
  uint32_t previousTick_ = 0;
  uint64_t uptimeMs_ = 0;
  portMUX_TYPE eventMux_ = portMUX_INITIALIZER_UNLOCKED;
  bool callbackConnected_ = false;
  bool callbackSubscribed_ = false;
  uint32_t disconnects_ = 0;
  int disconnectReason_ = 0;
};

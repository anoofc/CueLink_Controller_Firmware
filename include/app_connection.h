#pragma once
#include <NimBLEDevice.h>

#include <atomic>

#include "ble_protocol.h"
#include "buttons.h"
#include "config_manager.h"
#include "config_transfer.h"
#include "network.h"

class AppConnection {
 public:
  void attach(ConfigManager& manager) { manager_ = &manager; }
  bool authorizationOpen() const { return physical_.load(); }
  bool authenticated() const { return authenticated_.load(); }
  uint32_t passkey() const { return passkey_.load(); }
  void begin(const Network& network, Mode mode, uint32_t now);
  void update(const Network& network, Mode mode, bool discoveryRequested, uint32_t now);
  const char* stateText() const;
  const char* name() const { return name_; }
  uint32_t remainingSeconds(uint32_t now) const { return window_.remainingSeconds(now); }

 private:
  class ServerEvents : public NimBLEServerCallbacks {
   public:
    explicit ServerEvents(AppConnection& owner) : owner_(owner) {}
    uint32_t onPassKeyDisplay() override;
    void onAuthenticationComplete(NimBLEConnInfo&) override;
    void onConfirmPassKey(NimBLEConnInfo& peer, uint32_t) override {
      NimBLEDevice::injectConfirmPasskey(peer, false);
    }
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
  class ConfigEvents : public NimBLECharacteristicCallbacks {
   public:
    explicit ConfigEvents(AppConnection& owner) : owner_(owner) {}
    void onWrite(NimBLECharacteristic*, NimBLEConnInfo&) override;
    void onRead(NimBLECharacteristic*, NimBLEConnInfo&) override;
    void onSubscribe(NimBLECharacteristic*, NimBLEConnInfo&, uint16_t) override;

   private:
    AppConnection& owner_;
  } configEvents_{*this};
  bool secure(const NimBLEConnInfo& peer) const;
  void processConfiguration(uint32_t now);
  ConfigManager* manager_ = nullptr;
  Config::Transfer transfer_;
  NimBLECharacteristic* configResponse_ = nullptr;
  std::atomic<bool> physical_{false}, authenticated_{false};
  std::atomic<uint32_t> passkey_{0};
  uint8_t mailbox_[Config::maxFrame] = {}, responseFrame_[Config::maxFrame] = {};
  size_t mailboxFrameLimit_ = 20;
  bool configSubscribed_ = false;
  uint16_t configHandle_ = BLE_HS_CONN_HANDLE_NONE;
  uint32_t configWrites_ = 0, configReads_ = 0, configNotifications_ = 0;
  size_t mailboxSize_ = 0, responseSize_ = 0;
  bool mailboxRejected_ = false;
  uint32_t callbackGeneration_ = 0, processedGeneration_ = 0;
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

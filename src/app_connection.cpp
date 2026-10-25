#include "app_connection.h"

#include <algorithm>
#include <esp_mac.h>
#include <esp_system.h>

#include "serial_log.h"

void AppConnection::ServerEvents::onConnect(NimBLEServer* server, NimBLEConnInfo& peer) {
  // 15–30 ms, no peripheral latency, 4 s supervision; the central decides.
  server->updateConnParams(peer.getConnHandle(), 12, 24, 0, 400);
  portENTER_CRITICAL(&owner_.eventMux_);
  owner_.callbackConnected_ = true;
  owner_.callbackSubscribed_ = false;
  portEXIT_CRITICAL(&owner_.eventMux_);
}

void AppConnection::ServerEvents::onDisconnect(NimBLEServer*, NimBLEConnInfo&, int reason) {
  portENTER_CRITICAL(&owner_.eventMux_);
  owner_.callbackConnected_ = false;
  owner_.configSubscribed_ = false;
  owner_.configHandle_ = BLE_HS_CONN_HANDLE_NONE;
  owner_.callbackSubscribed_ = false;
  owner_.physical_ = false;
  owner_.authenticated_ = false;
  owner_.mailboxSize_ = 0;
  owner_.mailboxRejected_ = false;
  memset(owner_.mailbox_, 0, sizeof(owner_.mailbox_));
  owner_.responseSize_ = 0;
  ++owner_.callbackGeneration_;
  ++owner_.disconnects_;
  owner_.disconnectReason_ = reason;
  portEXIT_CRITICAL(&owner_.eventMux_);
}

void AppConnection::StatusEvents::onSubscribe(NimBLECharacteristic*, NimBLEConnInfo&,
                                              uint16_t value) {
  portENTER_CRITICAL(&owner_.eventMux_);
  owner_.callbackSubscribed_ = (value & 1) != 0;
  portEXIT_CRITICAL(&owner_.eventMux_);
}

void AppConnection::begin(const Network& network, Mode mode, uint32_t now) {
  uint8_t identity[6];
  if (esp_read_mac(identity, ESP_MAC_WIFI_STA) != ESP_OK) {
    FirmwareSerial.println("[BLE] Device identity unavailable; BLE disabled.");
    return;
  }
  snprintf(name_, sizeof(name_), "CueLink-%02X%02X", identity[4], identity[5]);
  if (!NimBLEDevice::init(name_)) {
    FirmwareSerial.println("[BLE] Initialization failed; BLE disabled.");
    return;
  }
  NimBLEDevice::setMTU(247);
  NimBLEDevice::setSecurityAuth(true, true, true);
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_DISPLAY_ONLY);
  auto* server = NimBLEDevice::createServer();
  server->setCallbacks(&serverEvents_, false);
  server->advertiseOnDisconnect(false);  // Only the main loop owns discovery.
  auto* service = server->createService(CueLinkBle::serviceUuid);
  auto* information = service->createCharacteristic(CueLinkBle::informationUuid,
                                                    NIMBLE_PROPERTY::READ, CueLinkBle::payloadSize);
  status_ = service->createCharacteristic(CueLinkBle::statusUuid,
                                          NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY,
                                          CueLinkBle::payloadSize);
  status_->setCallbacks(&statusEvents_);
  auto* request = service->createCharacteristic(
      "c0e10004-7a6b-4c8d-9e10-5b2f034a0001",
      NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_ENC | NIMBLE_PROPERTY::WRITE_AUTHEN, Config::maxFrame);
  request->setCallbacks(&configEvents_);
  configResponse_ = service->createCharacteristic(
      "c0e10005-7a6b-4c8d-9e10-5b2f034a0001",
      NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY | NIMBLE_PROPERTY::READ_ENC | NIMBLE_PROPERTY::READ_AUTHEN, Config::maxFrame);
  configResponse_->setCallbacks(&configEvents_);
  uint8_t encoded[CueLinkBle::payloadSize];
  CueLinkBle::encodeInformation(identity, encoded);
  information->setValue(encoded, sizeof(encoded));
  previousTick_ = now;
  uptimeMs_ = now;
  refresh(network, mode);
  service->start();
  server->start();
  advertising_ = NimBLEDevice::getAdvertising();
  advertising_->addServiceUUID(CueLinkBle::serviceUuid);
  // A 128-bit service UUID and name do not fit together in 31 advertising
  // bytes. Keep the filter UUID in the advertisement and name in scan response.
  NimBLEAdvertisementData response;
  response.setName(name_);
  advertising_->setScanResponseData(response);
  advertising_->enableScanResponse(true);
  available_ = true;
  FirmwareSerial.printf("[BLE] %s initialized; discovery off.\n", name_);
}

void AppConnection::refresh(const Network& network, Mode mode) {
  uint8_t ip[4] = {};
  if (network.connected()) {
    const IPAddress address = network.ip();
    for (uint8_t i = 0; i < 4; ++i) ip[i] = address[i];
  }
  int32_t rssi = network.rssi();
  if (rssi < -127) rssi = -127;
  if (rssi > 127) rssi = 127;
  uint8_t encoded[CueLinkBle::payloadSize];
  CueLinkBle::encodeStatus(static_cast<uint8_t>(network.state()), ip, int8_t(rssi),
                           uint32_t(uptimeMs_ / 1000), mode == Mode::Diagnostics, encoded);
  status_->setValue(encoded, sizeof(encoded));
}

void AppConnection::update(const Network& network, Mode mode, bool discoveryRequested,
                           uint32_t now) {
  uptimeMs_ += uint32_t(now - previousTick_);
  previousTick_ = now;
  if (!available_) return;
  window_.update(now);
  if (!window_.active() && !connected_) physical_ = false;
  processConfiguration(now);
  if (discoveryRequested) {
    window_.enable(now);
    if (!authenticated_) passkey_ = esp_random() % 1000000;
    physical_ = true;
    advertisingFailed_ = false;
    FirmwareSerial.println("[BLE] App discovery enabled for 120 seconds.");
  }
  portENTER_CRITICAL(&eventMux_);
  const bool connected = callbackConnected_;
  const bool subscribed = callbackSubscribed_;
  const uint32_t disconnects = disconnects_;
  const int reason = disconnectReason_;
  disconnects_ = 0;
  portEXIT_CRITICAL(&eventMux_);
  if (connected != connected_) {
    connected_ = connected;
    FirmwareSerial.printf("[BLE] App %s.\n", connected ? "connected" : "disconnected");
    refresh(network, mode);
  }
  if (disconnects)
    FirmwareSerial.printf("[BLE] Disconnect reason=%d; events=%lu\n", reason,
                          static_cast<unsigned long>(disconnects));
  const bool shouldAdvertise = window_.active() && !connected_ && !advertisingFailed_;
  if (shouldAdvertise && !advertising_->isAdvertising()) {
    if (!advertising_->start()) {
      advertisingFailed_ = true;
      FirmwareSerial.println("[BLE] Advertising failed; press B3 on App connection to retry.");
    }
  } else if (!shouldAdvertise && advertising_->isAdvertising()) {
    advertising_->stop();
    FirmwareSerial.println("[BLE] Discovery stopped.");
  }
  if (uint32_t(now - sampledAt_) >= 1000) {
    sampledAt_ = now;
    refresh(network, mode);
  }
  if (connected_ && subscribed && uint32_t(now - notifiedAt_) >= 1000) {
    notifiedAt_ = now;
    status_->notify();  // One best-effort sample per second, no notification queue.
  }
}

const char* AppConnection::stateText() const {
  if (!available_) return "Unavailable";
  if (connected_) return "Connected";
  if (advertisingFailed_) return "Off (retry B3)";
  return advertising_->isAdvertising() ? "Discoverable" : "Off";
}

bool AppConnection::secure(const NimBLEConnInfo& peer) const {
  return Config::authorized(physical_.load(), peer.isEncrypted(), peer.isAuthenticated(),
                            peer.getSecKeySize());
}
uint32_t AppConnection::ServerEvents::onPassKeyDisplay() { return owner_.passkey_.load(); }
void AppConnection::ServerEvents::onAuthenticationComplete(NimBLEConnInfo& peer) {
  owner_.authenticated_ = owner_.secure(peer);
  // Public status remains available to a secure bonded peer without a grant.
  // Physical authorization is still checked on every config frame and read.
  if (!peer.isEncrypted() || !peer.isAuthenticated() || peer.getSecKeySize() != 16)
    NimBLEDevice::getServer()->disconnect(peer.getConnHandle());
}
void AppConnection::ConfigEvents::onWrite(NimBLECharacteristic* characteristic,
                                          NimBLEConnInfo& peer) {
  const auto value = characteristic->getValue();
  const uint16_t mtu = peer.getMTU();
  const size_t frameLimit = std::min(size_t(mtu >= 23 ? mtu - 3 : 20), Config::maxFrame);
  portENTER_CRITICAL(&owner_.eventMux_);
  if (!owner_.secure(peer) || owner_.mailboxSize_ || value.size() < 8 || value.size() > Config::maxFrame) {
    owner_.mailboxRejected_ = true;
  } else {
    memcpy(owner_.mailbox_, value.data(), value.size());
    owner_.mailboxSize_ = value.size();
    owner_.mailboxFrameLimit_ = frameLimit;
    ++owner_.configWrites_;
    owner_.authenticated_ = true;
  }
  portEXIT_CRITICAL(&owner_.eventMux_);
  // Never leave credentials in the characteristic value after mailbox copy.
  characteristic->setValue(static_cast<const uint8_t*>(nullptr), 0);
}
void AppConnection::ConfigEvents::onRead(NimBLECharacteristic* characteristic,
                                         NimBLEConnInfo& peer) {
  uint8_t frame[Config::maxFrame];
  size_t size;
  portENTER_CRITICAL(&owner_.eventMux_);
  ++owner_.configReads_;
  size = owner_.responseSize_;
  memcpy(frame, owner_.responseFrame_, size);
  portEXIT_CRITICAL(&owner_.eventMux_);
  if (!owner_.secure(peer)) {
    const uint8_t denied[] = {1, 0x84, 0, 0, 0, 0, 0, 0};
    characteristic->setValue(denied, sizeof(denied));
  } else {
    owner_.authenticated_ = true;
    characteristic->setValue(frame, size);
  }
}
void AppConnection::processConfiguration(uint32_t now) {
  uint8_t bytes[Config::maxFrame];
  size_t frameLimit;
  size_t size;
  bool rejected;
  uint32_t generation;
  portENTER_CRITICAL(&eventMux_);
  size = mailboxSize_;
  frameLimit = mailboxFrameLimit_;
  memcpy(bytes, mailbox_, size);
  memset(mailbox_, 0, sizeof(mailbox_));
  mailboxSize_ = 0;
  rejected = mailboxRejected_;
  mailboxRejected_ = false;
  generation = callbackGeneration_;
  portEXIT_CRITICAL(&eventMux_);
  if (generation != processedGeneration_) {
    processedGeneration_ = generation;
    transfer_.reset();
  }
  if (rejected) {
    transfer_.reset();
    size = 0;
  }
  transfer_.update(now);
  if (size) transfer_.accept(bytes, size, physical_ && authenticated_, now, frameLimit);
  memset(bytes, 0, sizeof(bytes));
  if (manager_ && transfer_.complete()) {
    const auto& request = transfer_.request();
    std::string reply = manager_->execute(
        transfer_.id(),
        Config::crc32(reinterpret_cast<const uint8_t*>(request.data()), request.size()), request,
        now);
    if (reply.empty()) reply = "{\"ok\":true,\"pending\":true}";
    portENTER_CRITICAL(&eventMux_);
    const uint32_t writes = configWrites_, reads = configReads_;
    portEXIT_CRITICAL(&eventMux_);
    FirmwareSerial.printf("[ConfigTiming] request=%lu request_bytes=%u response_bytes=%u upload_chunks=%u download_chunks=%u ble_writes_total=%lu ble_reads_total=%lu notifications_total=%lu\n",
      static_cast<unsigned long>(transfer_.id()), unsigned(request.size()), unsigned(reply.size()), unsigned(transfer_.uploadChunks()),
      unsigned((reply.size() + transfer_.payloadSize() - 1) / transfer_.payloadSize()),
      static_cast<unsigned long>(writes), static_cast<unsigned long>(reads), static_cast<unsigned long>(configNotifications_));
    transfer_.reply(reply);
  }
  const std::string& frame = transfer_.frame();
  portENTER_CRITICAL(&eventMux_);
  const bool allowed = generation == callbackGeneration_ && callbackConnected_ && physical_ && authenticated_ && frame.size() <= Config::maxFrame;
  const bool changed = allowed && (responseSize_ != frame.size() ||
      memcmp(responseFrame_, frame.data(), frame.size()) != 0);
  responseSize_ = allowed && frame.size() <= Config::maxFrame ? frame.size() : 0;
  memcpy(responseFrame_, frame.data(), responseSize_);
  const bool notify = changed && configSubscribed_;
  const uint16_t handle = configHandle_;
  portEXIT_CRITICAL(&eventMux_);
  if (notify && handle != BLE_HS_CONN_HANDLE_NONE) {
    const auto peer = NimBLEDevice::getServer()->getPeerInfoByHandle(handle);
    // A custom targeted notification bypasses the characteristic read callback:
    // recheck the live link's encryption/authentication/key size before sending.
    if (peer.getConnHandle() == handle && secure(peer) && frame.size() <= size_t(peer.getMTU() - 3) &&
        configResponse_->notify(reinterpret_cast<const uint8_t*>(frame.data()), frame.size(), handle)) {
      ++configNotifications_;
      if (frame.size() == 10 && uint8_t(frame[1]) == 0x80 && frame[6] == 0 && frame[7] == 0)
        FirmwareSerial.printf("[ConfigTiming] mtu=%u frame_bytes=%u interval_units=%u latency=%u\n",
          peer.getMTU(), unsigned(uint8_t(frame[8]) | (uint16_t(uint8_t(frame[9])) << 8)),
          peer.getConnInterval(), peer.getConnLatency());
    }
  }
}

void AppConnection::ConfigEvents::onSubscribe(NimBLECharacteristic*, NimBLEConnInfo& peer, uint16_t value) {
  portENTER_CRITICAL(&owner_.eventMux_);
  owner_.configSubscribed_ = (value & 1) != 0;
  owner_.configHandle_ = peer.getConnHandle();
  portEXIT_CRITICAL(&owner_.eventMux_);
}

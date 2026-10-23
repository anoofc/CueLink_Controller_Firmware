#include "app_connection.h"
#include <esp_mac.h>

void AppConnection::ServerEvents::onConnect(NimBLEServer*, NimBLEConnInfo&) {
  portENTER_CRITICAL(&owner_.eventMux_);
  owner_.callbackConnected_ = true;
  owner_.callbackSubscribed_ = false;
  portEXIT_CRITICAL(&owner_.eventMux_);
}

void AppConnection::ServerEvents::onDisconnect(NimBLEServer*, NimBLEConnInfo&, int reason) {
  portENTER_CRITICAL(&owner_.eventMux_);
  owner_.callbackConnected_ = false;
  owner_.callbackSubscribed_ = false;
  ++owner_.disconnects_;
  owner_.disconnectReason_ = reason;
  portEXIT_CRITICAL(&owner_.eventMux_);
}

void AppConnection::StatusEvents::onSubscribe(NimBLECharacteristic*, NimBLEConnInfo&, uint16_t value) {
  portENTER_CRITICAL(&owner_.eventMux_);
  owner_.callbackSubscribed_ = (value & 1) != 0;
  portEXIT_CRITICAL(&owner_.eventMux_);
}

void AppConnection::begin(const Network& network, Mode mode, uint32_t now) {
  uint8_t identity[6];
  if (esp_read_mac(identity, ESP_MAC_WIFI_STA) != ESP_OK) {
    Serial.println("[BLE] Device identity unavailable; BLE disabled.");
    return;
  }
  snprintf(name_, sizeof(name_), "CueLink-%02X%02X", identity[4], identity[5]);
  if (!NimBLEDevice::init(name_)) {
    Serial.println("[BLE] Initialization failed; BLE disabled.");
    return;
  }
  NimBLEDevice::setSecurityAuth(false, false, false);  // No bonding or pairing store.
  auto* server = NimBLEDevice::createServer();
  server->setCallbacks(&serverEvents_, false);
  server->advertiseOnDisconnect(false);  // Only the main loop owns discovery.
  auto* service = server->createService(CueLinkBle::serviceUuid);
  auto* information = service->createCharacteristic(CueLinkBle::informationUuid, NIMBLE_PROPERTY::READ,
                                                     CueLinkBle::payloadSize);
  status_ = service->createCharacteristic(CueLinkBle::statusUuid,
                  NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY, CueLinkBle::payloadSize);
  status_->setCallbacks(&statusEvents_);
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
  Serial.printf("[BLE] %s initialized; discovery off.\n", name_);
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

void AppConnection::update(const Network& network, Mode mode, bool discoveryRequested, uint32_t now) {
  uptimeMs_ += uint32_t(now - previousTick_);
  previousTick_ = now;
  if (!available_) return;
  window_.update(now);
  if (discoveryRequested) {
    window_.enable(now);
    advertisingFailed_ = false;
    Serial.println("[BLE] App discovery enabled for 120 seconds.");
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
    Serial.printf("[BLE] App %s.\n", connected ? "connected" : "disconnected");
    refresh(network, mode);
  }
  if (disconnects) Serial.printf("[BLE] Disconnect reason=%d; events=%lu\n", reason,
                                  static_cast<unsigned long>(disconnects));
  const bool shouldAdvertise = window_.active() && !connected_ && !advertisingFailed_;
  if (shouldAdvertise && !advertising_->isAdvertising()) {
    if (!advertising_->start()) {
      advertisingFailed_ = true;
      Serial.println("[BLE] Advertising failed; press B3 on App connection to retry.");
    }
  } else if (!shouldAdvertise && advertising_->isAdvertising()) {
    advertising_->stop();
    Serial.println("[BLE] Discovery stopped.");
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

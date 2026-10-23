#include "network.h"
#include "WiFi.h"
#include <cassert>
#include <cstdio>
SerialStub Serial;
WiFiStub WiFi;

int main() {
  uint32_t now = UINT32_MAX - 10000;  // Attempt timeout crosses millis rollover.
  Network network;
  network.begin(now);
#ifdef TEST_UNCONFIGURED
  assert(network.state() == Network::State::NotConfigured);
  network.update(now + 100000);
  assert(WiFi.attempts == 0 && !WiFi.station && !WiFi.callback);
  assert(Serial.output.find("Wi-Fi not configured") != std::string::npos);
#else
  assert(WiFi.attempts == 1 && network.state() == Network::State::Connecting);
  // Association without DHCP must not become ready or expose addressing.
  WiFi.statusValue = WL_CONNECTED;
  network.update(now + 14999);
  assert(!network.connected() && uint32_t(network.ip()) == 0);
  now += 15000;
  network.update(now);
  assert(WiFi.cancellations == 1 && network.state() == Network::State::Reconnecting);
  const uint32_t delays[] = {2000, 4000, 8000, 16000, 30000, 30000};
  for (uint32_t delay : delays) {
    unsigned attempts = WiFi.attempts;
    network.update(now + delay - 1);
    assert(WiFi.attempts == attempts);
    now += delay;
    network.update(now);
    assert(WiFi.attempts == attempts + 1);
    network.update(now + 14999);
    assert(WiFi.attempts == attempts + 1);  // No overlapping attempts.
    now += 15000;
    network.update(now);
    assert(network.losses() == 0);  // Initial failures are not connection losses.
  }
  now += 30000;
  network.update(now);
  WiFi.statusValue = WL_CONNECTED;
  WiFi.address = IPAddress(1);
  network.update(++now);
  assert(network.connected() && uint32_t(network.ip()) == 1);
  assert(uint32_t(network.subnet()) == 2 && uint32_t(network.gateway()) == 3);
  assert(uint32_t(network.dns()) == 4 && network.rssi() == -60 && network.channel() == 6);
  // Disconnect arrives from a callback: nothing is logged or rendered there.
  const size_t logged = Serial.output.size();
  WiFi.statusValue = WL_DISCONNECTED;
  WiFi.emit(ARDUINO_EVENT_WIFI_STA_DISCONNECTED, 201);
  assert(Serial.output.size() == logged);
  network.update(++now);
  assert(!network.connected() && network.losses() == 1);
  assert(uint32_t(network.ip()) == 0 && uint32_t(network.subnet()) == 0);
  assert(uint32_t(network.gateway()) == 0 && uint32_t(network.dns()) == 0);
  assert(network.channel() == 0);
  assert(Serial.output.find("reason=201") != std::string::npos);
  network.update(now + 1999);
  unsigned attempts = WiFi.attempts;
  now += 2000;  // A successful connection resets backoff to two seconds.
  network.update(now);
  assert(WiFi.attempts == attempts + 1 && network.losses() == 1);
  WiFi.statusValue = WL_CONNECTED;
  network.update(++now);
  assert(network.connected());
  WiFi.emit(ARDUINO_EVENT_WIFI_STA_LOST_IP);
  network.update(++now);
  assert(!network.connected() && network.losses() == 2);
  network.update(++now);
  assert(network.losses() == 2);  // Cancellation callback does not count twice.
#endif
  std::puts("Network lifecycle checks passed.");
}

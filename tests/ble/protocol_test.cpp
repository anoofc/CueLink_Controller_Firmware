#include "ble_protocol.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void output(const char* name, const uint8_t (&bytes)[CueLinkBle::payloadSize], bool last) {
  printf("\"%s\":[", name);
  for (unsigned i = 0; i < sizeof(bytes); ++i) printf("%s%u", i ? "," : "", bytes[i]);
  printf("]%s\n", last ? "" : ",");
}
int main() {
  using namespace CueLinkBle;
  static_assert(payloadSize <= 20, "Default ATT payload exceeded");
  assert(strlen(serviceUuid) == 36 && strcmp(serviceUuid, informationUuid) != 0);
  uint8_t bytes[payloadSize];
  const uint8_t identity[6] = {2, 17, 34, 51, 68, 85};
  const uint8_t ip[4] = {192, 0, 2, 1};
  puts("{");
  encodeInformation(identity, bytes);
  output("information", bytes, false);
  encodeStatus(2, ip, -52, 0x01020304, false, bytes);
  output("connected", bytes, false);
  encodeStatus(3, ip, -52, UINT32_MAX, true, bytes);
  output("disconnected", bytes, true);
  puts("}");
  assert(bytes[2] == 0 && bytes[6] == 128 && bytes[11] == 1);
  DiscoveryWindow window;
  assert(!window.active());
  const uint32_t opened = UINT32_MAX - 1000;
  window.enable(opened);
  window.update(opened + 119999);
  assert(window.active() && window.remainingSeconds(opened + 119999) == 1);
  window.update(opened + 120000);
  assert(!window.active() && window.remainingSeconds(opened + 120000) == 0);
  window.update(opened + 100);  // No reactivation when millis cycles again.
  assert(!window.active());
  window.enable(30);
  window.update(100000);
  window.enable(100000);  // Only another explicit B3 request renews the window.
  window.update(120030);
  assert(window.active());
}

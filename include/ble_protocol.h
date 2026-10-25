#pragma once
#include <stdint.h>

namespace CueLinkBle {
constexpr char serviceUuid[] = "c0e10001-7a6b-4c8d-9e10-5b2f034a0001";
constexpr char informationUuid[] = "c0e10002-7a6b-4c8d-9e10-5b2f034a0001";
constexpr char statusUuid[] = "c0e10003-7a6b-4c8d-9e10-5b2f034a0001";
constexpr uint8_t protocolVersion = 1;
constexpr uint8_t firmwareMajor = 0, firmwareMinor = 5, firmwarePatch = 0;
constexpr uint8_t payloadSize = 12;
constexpr uint16_t capabilities = 0x00ff;  // Bit 7: MTU-aware v2 notifications + scoped readback. Bits 5/6: authenticated configuration / network staging+scan.

inline void encodeInformation(const uint8_t (&identity)[6], uint8_t (&out)[payloadSize]) {
  out[0] = protocolVersion;
  for (uint8_t i = 0; i < 6; ++i) out[i + 1] = identity[i];
  out[7] = firmwareMajor;
  out[8] = firmwareMinor;
  out[9] = firmwarePatch;
  out[10] = uint8_t(capabilities);
  out[11] = uint8_t(capabilities >> 8);
}

inline void encodeStatus(uint8_t wifiState, const uint8_t (&ip)[4], int8_t rssi,
                         uint32_t uptimeSeconds, bool diagnostics,
                         uint8_t (&out)[payloadSize]) {
  out[0] = protocolVersion;
  out[1] = wifiState;
  for (uint8_t i = 0; i < 4; ++i) out[i + 2] = wifiState == 2 ? ip[i] : 0;
  out[6] = wifiState == 2 ? uint8_t(rssi) : 0x80;
  for (uint8_t i = 0; i < 4; ++i) out[i + 7] = uint8_t(uptimeSeconds >> (i * 8));
  out[11] = diagnostics ? 1 : 0;
}

class DiscoveryWindow {
 public:
  static constexpr uint32_t durationMs = 120000;
  void enable(uint32_t now) { openedAt_ = now; enabled_ = true; }
  void update(uint32_t now) {
    if (enabled_ && uint32_t(now - openedAt_) >= durationMs) enabled_ = false;
  }
  bool active() const { return enabled_; }
  uint32_t remainingSeconds(uint32_t now) const {
    if (!enabled_ || uint32_t(now - openedAt_) >= durationMs) return 0;
    return (durationMs - uint32_t(now - openedAt_) + 999) / 1000;
  }
 private:
  bool enabled_ = false;
  uint32_t openedAt_ = 0;
};
}  // namespace CueLinkBle

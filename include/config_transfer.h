#pragma once
#include <stddef.h>
#include <stdint.h>

#include <string>

#include "configuration.h"
namespace Config {
inline bool authorized(bool physical, bool encrypted, bool authenticated, unsigned keyBytes) {
  return physical && encrypted && authenticated && keyBytes == 16;
}
// Every frame: version, kind, LE request uint32, LE sequence uint16, body.
// V1: 20-byte responses / 12-byte payloads. V2: MTU-bounded frames up to 244 bytes.
constexpr size_t maxFrame = 244;
class Transfer {
 public:
  void accept(const uint8_t* data, size_t size, bool authorized, uint32_t now, size_t frameLimit = 20);
  void update(uint32_t now);
  void reset();
  bool complete() const { return complete_; }
  const std::string& request() const { return input_; }
  uint32_t id() const { return id_; }
  uint16_t uploadChunks() const { return next_; }
  size_t payloadSize() const { return payload_; }
  void reply(const std::string& value);
  const std::string& frame() const { return frame_; }

 private:
  void status(uint8_t kind, uint16_t sequence = 0);
  std::string input_, output_, frame_;
  uint32_t id_ = 0, total_ = 0, crc_ = 0, at_ = 0;
  uint16_t next_ = 0;
  uint8_t version_ = 1;
  size_t payload_ = 12;
  bool receiving_ = false, complete_ = false;
};
}  // namespace Config

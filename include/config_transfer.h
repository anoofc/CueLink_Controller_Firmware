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
// Maximum frame 128 bytes; the app deliberately uses 20 for default-MTU safety.
class Transfer {
 public:
  void accept(const uint8_t* data, size_t size, bool authorized, uint32_t now);
  void update(uint32_t now);
  void reset();
  bool complete() const { return complete_; }
  const std::string& request() const { return input_; }
  uint32_t id() const { return id_; }
  void reply(const std::string& value);
  std::string frame() const { return frame_; }

 private:
  void status(uint8_t kind, uint16_t sequence = 0);
  std::string input_, output_, frame_;
  uint32_t id_ = 0, total_ = 0, crc_ = 0, at_ = 0;
  uint16_t next_ = 0;
  bool receiving_ = false, complete_ = false;
};
}  // namespace Config

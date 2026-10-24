#include "config_transfer.h"

#include <algorithm>
namespace Config {
static uint32_t read32(const uint8_t* d) {
  return uint32_t(d[0]) | (uint32_t(d[1]) << 8) | (uint32_t(d[2]) << 16) | (uint32_t(d[3]) << 24);
}
static void put32(std::string& s, uint32_t n) {
  for (int b = 0; b < 4; ++b) s += char(n >> (8 * b));
}
void Transfer::status(uint8_t kind, uint16_t sequence) {
  frame_.clear();
  frame_ += char(1);
  frame_ += char(kind);
  put32(frame_, id_);
  frame_ += char(sequence);
  frame_ += char(sequence >> 8);
}
void Transfer::reset() {
  std::fill(input_.begin(), input_.end(), '\0');
  input_.clear();
  output_.clear();
  receiving_ = complete_ = false;
  status(0x83);
}
void Transfer::update(uint32_t now) {
  if ((receiving_ || complete_ || !output_.empty()) && uint32_t(now - at_) >= 15000) reset();
}
void Transfer::reply(const std::string& value) {
  complete_ = false;
  receiving_ = false;
  std::fill(input_.begin(), input_.end(), '\0');
  input_.clear();
  output_ = value;
  if (output_.size() > maxTransfer) {
    output_ = "{\"ok\":false,\"error\":\"Response too large\"}";
  }
  status(0x81);
  put32(frame_, output_.size());
  put32(frame_, crc32(reinterpret_cast<const uint8_t*>(output_.data()), output_.size()));
}
void Transfer::accept(const uint8_t* data, size_t size, bool authorized, uint32_t now) {
  if (!authorized) {
    reset();
    status(0x84);
    return;
  }
  update(now);
  if (size < 8 || size > 128 || data[0] != 1) {
    reset();
    return;
  }
  uint32_t id = read32(data + 2);
  uint16_t seq = uint16_t(data[6]) | (uint16_t(data[7]) << 8);
  uint8_t kind = data[1];
  if (kind == 1) {
    if (receiving_ || complete_ || size != 16 || !id || seq) {
      reset();
      return;
    }
    id_ = id;
    total_ = read32(data + 8);
    crc_ = read32(data + 12);
    if (!total_ || total_ > maxTransfer) {
      reset();
      return;
    }
    std::fill(input_.begin(), input_.end(), '\0');
    input_.clear();
    output_.clear();
    next_ = 0;
    receiving_ = true;
    at_ = now;
    status(0x80, 0);
    return;
  }
  if (id != id_) {
    reset();
    return;
  }
  if (kind == 4) {
    reset();
    if (size != 8) return;
    status(0x80, seq);
    return;
  }
  if (kind == 5) {
    if (output_.empty() || size != 8 || size_t(seq) * 12 >= output_.size()) {
      reset();
      return;
    }
    at_ = now;
    status(0x82, seq);
    frame_ += output_.substr(size_t(seq) * 12, 12);
    return;
  }
  if (!receiving_ || seq != next_) {
    reset();
    return;
  }
  at_ = now;
  if (kind == 2 && size > 8 && input_.size() + size - 8 <= total_) {
    input_.append(reinterpret_cast<const char*>(data + 8), size - 8);
    ++next_;
    status(0x80, next_);
    return;
  }
  if (kind == 3 && size == 8 && input_.size() == total_ &&
      crc32(reinterpret_cast<const uint8_t*>(input_.data()), input_.size()) == crc_) {
    receiving_ = false;
    complete_ = true;
    status(0x85, next_);
    return;
  }
  reset();
}
}  // namespace Config

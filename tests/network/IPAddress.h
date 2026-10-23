#pragma once
#include <cstdint>
#include <string>
class IPAddress {
 public:
  IPAddress(uint32_t value = 0) : value_(value) {}
  operator uint32_t() const { return value_; }
  std::string toString() const { return std::to_string(value_); }
 private:
  uint32_t value_;
};

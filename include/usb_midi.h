#pragma once
#include <atomic>
#include "midi.h"
class UsbMidiPort : public Midi::Port {
 public:
  bool begin();
  bool mounted() const override;
  bool ready() const override;
  uint32_t sessionEpoch() const override { return epoch_.load(); }
  bool send(const Midi::Packet& packet) override;
 private:
  std::atomic<uint32_t> epoch_{0};
  bool initialized_ = false;
};

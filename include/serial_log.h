#pragma once
#ifdef ARDUINO
#include <Arduino.h>
// Bounded best-effort CDC logging, separate from every cue transport.
class SerialLog : public Print {
 public:
  void begin(uint32_t baud);
  void update();
  explicit operator bool() const;
  size_t write(uint8_t value) override { return write(&value, 1); }
  size_t write(const uint8_t* bytes, size_t count) override;
  using Print::write;
  uint32_t droppedBytes() const { return droppedBytes_; }
 private:
  uint8_t buffer_[4096];
  size_t head_ = 0, size_ = 0;
  uint32_t droppedBytes_ = 0;
};
extern SerialLog FirmwareSerial;
#else
#define FirmwareSerial Serial
#endif

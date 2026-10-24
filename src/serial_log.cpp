#include "serial_log.h"
#include <USBCDC.h>
#include <tusb.h>

// CDC-on-boot is disabled so both interfaces register before USB.begin().
static USBCDC cdc;
SerialLog FirmwareSerial;
void SerialLog::begin(uint32_t baud) { cdc.begin(baud); }
SerialLog::operator bool() const { return bool(cdc); }
size_t SerialLog::write(const uint8_t* bytes, size_t count) {
  if (!bool(cdc)) return 0;  // Never retain offline logs for a future monitor.
  if (count > sizeof(buffer_) - size_) { droppedBytes_ += count; return 0; }
  for (size_t i = 0; i < count; ++i) buffer_[(head_ + size_ + i) % sizeof(buffer_)] = bytes[i];
  size_ += count;
  return count;
}
void SerialLog::update() {
  if (!tud_cdc_connected()) { head_ = size_ = 0; return; }
  // The installed USBCDC::write() spins when its TX FIFO is full. Bypass that
  // path: one bounded, nonblocking TinyUSB write per loop, at most 64 bytes.
  size_t count = tud_cdc_write_available();
  if (count > size_) count = size_;
  if (count > 64) count = 64;
  if (count > sizeof(buffer_) - head_) count = sizeof(buffer_) - head_;
  if (!count) return;
  const size_t written = tud_cdc_write(buffer_ + head_, count);
  head_ = (head_ + written) % sizeof(buffer_);
  size_ -= written;
  tud_cdc_write_flush();
}

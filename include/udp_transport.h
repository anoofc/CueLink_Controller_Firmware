#pragma once

#include "command.h"

// A single reusable nonblocking datagram socket. No receiver, command queue,
// acknowledgement, or retry logic. Port zero asks the OS to choose a source.
class UdpTransport {
 public:
  ~UdpTransport() { stop(); }
  UdpTransport() = default;
  UdpTransport(const UdpTransport&) = delete;
  UdpTransport& operator=(const UdpTransport&) = delete;
  bool open(const Command::ButtonMapping* mappings, size_t count);
  bool send(const Command::IPv4& address, uint16_t port, const Command::Packet& packet);
  void stop();
  bool active() const { return socket_ >= 0; }
  uint16_t localPort() const { return localPort_; }
 private:
  int socket_ = -1;
  uint16_t localPort_ = 0;
};

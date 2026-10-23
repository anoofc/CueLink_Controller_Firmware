#include "udp_transport.h"

#include <fcntl.h>
#include <string.h>
#ifdef ARDUINO
#include <lwip/sockets.h>
#else
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

void UdpTransport::stop() {
  if (socket_ >= 0) close(socket_);
  socket_ = -1;
  localPort_ = 0;
}

bool UdpTransport::open(const Command::ButtonMapping* mappings, size_t count) {
  if (active()) return true;
  // Rare source/destination collisions are resolved before sending anything.
  // Keep even socket setup bounded; a later press may try setup again.
  for (uint8_t attempt = 0; attempt < 8; ++attempt) {
    socket_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socket_ < 0) return false;
    sockaddr_in local{};
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = htonl(INADDR_ANY);
    local.sin_port = 0;
    const int flags = fcntl(socket_, F_GETFL, 0);
    if (flags < 0 || fcntl(socket_, F_SETFL, flags | O_NONBLOCK) < 0 ||
        bind(socket_, reinterpret_cast<sockaddr*>(&local), sizeof(local)) < 0) {
      stop();
      return false;
    }
    socklen_t length = sizeof(local);
    if (getsockname(socket_, reinterpret_cast<sockaddr*>(&local), &length) < 0) {
      stop();
      return false;
    }
    localPort_ = ntohs(local.sin_port);
    bool distinct = localPort_ != 0;
    for (size_t i = 0; i < count; ++i) {
      if (mappings[i].destination.port == localPort_) distinct = false;
    }
    if (distinct) return true;
    stop();
  }
  return false;
}

bool UdpTransport::send(const Command::IPv4& address, uint16_t port,
                        const Command::Packet& packet) {
  if (!active() || !port || !packet.length || packet.length > Command::maxPacketSize) {
    return false;
  }
  sockaddr_in remote{};
  remote.sin_family = AF_INET;
  remote.sin_port = htons(port);
  const uint8_t* octets = address.octets;
  const uint32_t hostOrder = (uint32_t(octets[0]) << 24) | (uint32_t(octets[1]) << 16) |
                             (uint32_t(octets[2]) << 8) | octets[3];
  remote.sin_addr.s_addr = htonl(hostOrder);
  // One datagram, one submission attempt. EAGAIN and all other local errors
  // are failures; no buffered command is retained for a later loop iteration.
  const int sent = sendto(socket_, packet.bytes, packet.length, 0,
                          reinterpret_cast<sockaddr*>(&remote), sizeof(remote));
  return sent >= 0 && static_cast<size_t>(sent) == packet.length;
}

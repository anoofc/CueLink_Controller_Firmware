#include "udp_transport.h"
#include "fixtures.h"

#include <arpa/inet.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <initializer_list>
#include <sys/socket.h>
#include <unistd.h>

int main() {
  const int receiver = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  assert(receiver >= 0);
  sockaddr_in local{};
  local.sin_family = AF_INET;
  local.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  assert(bind(receiver, reinterpret_cast<sockaddr*>(&local), sizeof(local)) == 0);
  socklen_t length = sizeof(local);
  assert(getsockname(receiver, reinterpret_cast<sockaddr*>(&local), &length) == 0);
  timeval timeout{1, 0};
  assert(setsockopt(receiver, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) == 0);
  const uint16_t destinationPort = ntohs(local.sin_port);
  TestCommands::Mapping mappings[4];
  memcpy(mappings, TestCommands::buttons, sizeof(mappings));
  mappings[0].destination.port = destinationPort;

  const uint16_t ports[] = {destinationPort, 7000, 9000};
  UdpTransport transport;
  assert(transport.open(ports, 3));
  const uint16_t source = transport.localPort();
  for (const auto& mapping : mappings) assert(source != mapping.destination.port);
  assert(source != 0 && transport.open(ports, 3) && transport.localPort() == source);
  Command::IPv4 address;
  address.octets[0] = 127;
  address.octets[3] = 1;  // Native host transport test only; not user configuration.
  for (uint8_t button : {uint8_t(2), uint8_t(0)}) {
    Command::Packet packet;
    assert(Command::encode(mappings[button].action, packet) == Command::EncodeStatus::Ok);
    assert(transport.send(address, destinationPort, packet));
    uint8_t received[1025];
    sockaddr_in sender{};
    length = sizeof(sender);
    const auto count = recvfrom(receiver, received, sizeof(received), 0,
                                reinterpret_cast<sockaddr*>(&sender), &length);
    assert(count == static_cast<ssize_t>(packet.length));
    assert(memcmp(received, packet.bytes, packet.length) == 0);
    assert(ntohs(sender.sin_port) == source);
  }
  transport.stop();
  assert(!transport.active() && transport.localPort() == 0);
  assert(transport.open(ports, 3));
  transport.stop();
  close(receiver);
  puts("Native UDP datagram bytes, reusable source port, and socket lifecycle passed.");
}

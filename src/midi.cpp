#include "midi.h"
#include "serial_log.h"
#ifndef ARDUINO
#include <Arduino.h>
#endif

bool Midi::encode(const Message& message, Packet& packet) {
  if (message.channel < 1 || message.channel > 16 || message.data1 > 127 || message.data2 > 127) return false;
  uint8_t status;
  switch (message.type) {
    case Type::NotePulse:
      if (!message.data2 || !message.durationMs || message.durationMs > 60000) return false;
      status = 0x90; break;
    case Type::NoteOn: status = 0x90; break;
    case Type::NoteOff: status = 0x80; break;
    case Type::ControlChange: status = 0xb0; break;
    case Type::ProgramChange:
      if (message.data2) return false;
      status = 0xc0; break;
    default: return false;
  }
  packet.bytes[0] = status >> 4;  // USB-MIDI 1.0 cable 0 and Code Index Number.
  packet.bytes[1] = status | uint8_t(message.channel - 1);
  packet.bytes[2] = uint8_t(message.data1);
  packet.bytes[3] = message.type == Type::ProgramChange ? 0 : uint8_t(message.data2);
  return true;
}
void Midi::Engine::begin() {
  for (auto& pending : pending_) pending.active = false;
  epoch_ = port_.sessionEpoch();
}
size_t Midi::Engine::pendingCount() const {
  size_t count = 0;
  for (const auto& pending : pending_) if (pending.active) ++count;
  return count;
}
void Midi::Engine::observeConnection() {
  const uint32_t epoch = port_.sessionEpoch();
  if (!port_.ready() || epoch != epoch_) {
    for (auto& pending : pending_) {
      if (pending.active) ++droppedReleases_;
      pending.active = false;
    }
  }
  epoch_ = epoch;
}
void Midi::Engine::update(uint32_t now) {
  observeConnection();
  for (auto& pending : pending_) {
    if (!pending.active || uint32_t(now - pending.at) < pending.duration) continue;
    pending.active = false;  // Always consume the release, including failures.
    Packet packet;
    encode(Message::noteOff(pending.channel, pending.note), packet);
    lastRelease_.valid = true;
    lastRelease_.channel = pending.channel;
    lastRelease_.note = pending.note;
    lastRelease_.at = now;
    lastRelease_.result = port_.ready() && port_.send(packet) ? Result::Submitted : Result::TransportFailure;
    if (lastRelease_.result != Result::Submitted) ++releaseFailures_;
    FirmwareSerial.printf("[MIDI OFF] ch=%u note=%u result=%s\n", pending.channel, pending.note,
                          lastRelease_.result == Result::Submitted ? "Local submission" : "Local failure (no retry)");
  }
}
Midi::Result Midi::Engine::submit(const Message& message, bool readyAtEvent, uint32_t now) {
  Packet packet;
  if (!encode(message, packet)) return Result::Invalid;
  observeConnection();
  if (!readyAtEvent || !port_.ready()) return Result::Unavailable;
  Pending* existing = nullptr;
  Pending* free = nullptr;
  for (auto& pending : pending_) {
    if (pending.active && pending.channel == message.channel && pending.note == message.data1) existing = &pending;
    if (!pending.active && !free) free = &pending;
  }
  if (message.type == Type::NotePulse && !existing && !free) return Result::SchedulerFull;
  if (!port_.send(packet)) return Result::TransportFailure;
  if (message.type == Type::NotePulse) {
    Pending& pending = existing ? *existing : *free;
    pending.active = true;
    pending.channel = message.channel;
    pending.note = message.data1;
    pending.at = now;
    pending.duration = message.durationMs;
  } else if ((message.type == Type::NoteOn || message.type == Type::NoteOff) && existing) {
    // Explicit successful note messages supersede any older pulse timer.
    existing->active = false;
  }
  return Result::Submitted;
}

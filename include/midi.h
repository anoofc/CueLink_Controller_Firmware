#pragma once
#include <stdint.h>
#include <stddef.h>

namespace Midi {
enum class Type { NotePulse, NoteOn, NoteOff, ControlChange, ProgramChange };
struct Message {
  Type type;
  uint16_t channel;  // User-facing 1..16; converted only when encoding.
  uint16_t data1;    // Note, controller number, or 0-based program (0..127).
  uint16_t data2;    // Velocity or CC value (0..127); unused for Program Change.
  uint32_t durationMs;
  static constexpr Message pulse(uint16_t channel, uint16_t note, uint16_t velocity, uint32_t ms = 100) {
    return {Type::NotePulse, channel, note, velocity, ms};
  }
  static constexpr Message noteOn(uint16_t channel, uint16_t note, uint16_t velocity) {
    return {Type::NoteOn, channel, note, velocity, 0};
  }
  static constexpr Message noteOff(uint16_t channel, uint16_t note, uint16_t velocity = 0) {
    return {Type::NoteOff, channel, note, velocity, 0};
  }
  static constexpr Message controlChange(uint16_t channel, uint16_t controller, uint16_t value) {
    return {Type::ControlChange, channel, controller, value, 0};
  }
  static constexpr Message programChange(uint16_t channel, uint16_t program) {
    return {Type::ProgramChange, channel, program, 0, 0};
  }
};
struct Packet { uint8_t bytes[4]; };
bool encode(const Message& message, Packet& packet);
class Port {
 public:
  virtual ~Port() = default;
  virtual bool mounted() const = 0;
  virtual bool ready() const = 0;
  virtual uint32_t sessionEpoch() const = 0;
  virtual bool send(const Packet& packet) = 0;
};
enum class Result { Submitted, Invalid, Unavailable, TransportFailure, SchedulerFull };
struct Release {
  bool valid = false;
  uint8_t channel = 0, note = 0;
  Result result = Result::Submitted;
  uint32_t at = 0;
};
class Engine {
 public:
  static constexpr size_t capacity = 16;
  explicit Engine(Port& port) : port_(port) {}
  void begin();
  void update(uint32_t now);
  Result submit(const Message& message, bool readyAtEvent, uint32_t now);
  bool ready() const { return port_.ready(); }
  bool mounted() const { return port_.mounted(); }
  size_t pendingCount() const;
  const Release& lastRelease() const { return lastRelease_; }
  uint32_t releaseFailures() const { return releaseFailures_; }
  uint32_t droppedReleases() const { return droppedReleases_; }
 private:
  struct Pending { bool active = false; uint8_t channel = 0, note = 0; uint32_t at = 0, duration = 0; };
  void observeConnection();
  Port& port_;
  Pending pending_[capacity];
  uint32_t epoch_ = 0;
  uint32_t releaseFailures_ = 0, droppedReleases_ = 0;
  Release lastRelease_;
};
}  // namespace Midi

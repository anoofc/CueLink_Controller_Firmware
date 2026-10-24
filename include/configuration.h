#pragma once
#include <ArduinoJson.h>

#include <memory>
#include <string>
#include <vector>

#include "triggers.h"
namespace Config {
constexpr size_t maxTransfer = 16384, maxConfiguration = 12288, documentCapacity = 32768;
constexpr uint32_t schema = 1;
uint32_t crc32(const uint8_t* bytes, size_t size);
std::string json(JsonVariantConst value);
bool ipv4(const char* text, uint32_t& value, bool zero = false);
bool validSsid(const std::string& value);
bool validateNetwork(JsonVariantConst value, std::string& error);
bool credential(JsonVariantConst change, const std::string& previous, std::string& next);
class Snapshot {
 public:
  Snapshot();
  DynamicJsonDocument document{documentCapacity};
  Triggers::Button buttons[4];
  Triggers::Configuration view{buttons};
  bool parse(const std::string& bytes, std::string& error);
  bool build(std::string& error);
  void development();

 private:
  struct Slot {
    Triggers::Osc osc{};
    Triggers::Udp udp{};
    Midi::Message midi{};
    Command::Argument args[8];
    Command::Destination destinations[8];
    std::vector<uint8_t> bytes;
  } slots_[4][4];
  Triggers::Trigger triggers_[4][4];
  Command::Destination master_[8];
};
class Storage {
 public:
  virtual ~Storage() = default;
  virtual std::string read(unsigned slot) = 0;
  virtual bool write(unsigned slot, const std::string& bytes) = 0;
};
struct Record {
  uint32_t revision = 0, request = 0, fingerprint = 0;
  std::string configuration, password;
};
std::string encodeRecord(const Record& record);
bool decodeRecord(const std::string& bytes, Record& record);
class Journal {
 public:
  explicit Journal(Storage& storage) : storage_(storage) {}
  bool load(Record& record);
  bool save(const Record& record);

 private:
  Storage& storage_;
  unsigned next_ = 0;
};
}  // namespace Config

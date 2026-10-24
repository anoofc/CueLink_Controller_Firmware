#pragma once
#include "commands.h"
#include "configuration.h"
class ConfigManager {
 public:
  explicit ConfigManager(Config::Storage& storage) : journal_(storage) {}
  void begin(Network& network, Commands& commands, uint32_t now);
  std::string execute(uint32_t request, uint32_t fingerprint, const std::string& bytes,
                      uint32_t now);
  void update(uint32_t now);
  bool pending() const { return pending_; }
  bool completed() const { return completed_; }
  std::string takeReply() {
    completed_ = false;
    return reply_;
  }
  const Config::Record& record() const { return record_; }

 private:
  std::string response(bool ok, const char* error = "", bool includeConfiguration = false);
  std::string commit(uint32_t request, uint32_t fingerprint);
  NetworkSettings networkSettings(const Config::Snapshot& snapshot, const std::string& password);
  Config::Journal journal_;
  Config::Record record_;
  std::unique_ptr<Config::Snapshot> active_, candidate_;
  Network* network_ = nullptr;
  Commands* commands_ = nullptr;
  bool pending_ = false, completed_ = false, scan_ = false;
  uint32_t started_ = 0, pendingRequest_ = 0, pendingFingerprint_ = 0;
  std::string candidatePassword_, reply_, lastError_;
  uint32_t lastRequest_ = 0;
};
#ifdef ARDUINO
#include <Preferences.h>
class NvsConfigStorage : public Config::Storage {
 public:
  bool begin() { return preferences_.begin("cuelink5", false, "cuelinkcfg"); }
  std::string read(unsigned slot) override;
  bool write(unsigned slot, const std::string& bytes) override;

 private:
  Preferences preferences_;
};
#endif

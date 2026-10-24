#pragma once
#include <string>
struct NetworkSettings {
  std::string ssid, password;
  bool dhcp = true;
  std::string ip = "0.0.0.0", subnet = "0.0.0.0", gateway = "0.0.0.0", dns1 = "0.0.0.0",
              dns2 = "0.0.0.0";
};

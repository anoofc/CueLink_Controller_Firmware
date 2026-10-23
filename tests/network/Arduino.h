#pragma once
#include <cstdint>
#include <cstdio>
#include <string>
struct SerialStub {
  std::string output;
  void println(const char* value) { output += value; output += '\n'; }
  template <typename... Args> void printf(const char* format, Args... args) {
    char text[512];
    std::snprintf(text, sizeof(text), format, args...);
    output += text;
  }
};
extern SerialStub Serial;

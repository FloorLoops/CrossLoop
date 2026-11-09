#pragma once
#include <cstdint>
#include <cstdio>
#include <string>
// The store test needs a stable path key, not a cryptographic primitive.
class MD5Builder {
 public:
  void begin() { value = 2166136261u; }
  void add(const uint8_t* data, size_t count) {
    for (size_t i = 0; i < count; ++i) value = (value ^ data[i]) * 16777619u;
  }
  void calculate() {}
  std::string toString() const {
    char out[33];
    snprintf(out, sizeof(out), "%032x", value);
    return out;
  }

 private:
  uint32_t value = 0;
};

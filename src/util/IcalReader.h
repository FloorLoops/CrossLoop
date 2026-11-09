#pragma once

#include <HalStorage.h>

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace crossloop::calendar {
enum class LineResult { Line, End, Invalid, TooLarge, StorageError };
// Own on the heap: folded lines may be up to 8 KiB. The fixed 512-byte input
// buffer bounds SD calls; no whole-calendar allocation or per-line growth.
class IcalReader {
 public:
  bool open(const char* path);
  LineResult next();
  char* text() { return line; }
  size_t length() const { return used; }
  void close() { file.close(); }

 private:
  int peek();
  int take();
  HalFile file;
  uint8_t input[512]{};
  char line[8193]{};
  size_t at = 0, size = 0, used = 0;
  bool first = true;
  bool failed = false;
};
struct Property {
  std::string_view name, parameters, value;
};
bool property(std::string_view line, Property& result);
bool same(std::string_view left, std::string_view right);
// Return one named parameter. A duplicate match or malformed value fails.
bool parameter(const Property& property, std::string_view name, std::string_view& value);
bool copyText(std::string_view value, char* destination, size_t capacity, bool truncate);
}  // namespace crossloop::calendar

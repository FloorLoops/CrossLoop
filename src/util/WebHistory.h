#pragma once

#include <array>
#include <cstddef>
#include <cstring>
#include <string_view>

namespace crossloop::web {
// Session-only history owns cache keys, never page bodies or URL copies.
class History {
 public:
  static constexpr size_t CAPACITY = 32;
  const char* current() const { return count ? entries[cursor].data() : ""; }
  const char* back() const { return canBack() ? entries[cursor - 1].data() : ""; }
  const char* forward() const { return canForward() ? entries[cursor + 1].data() : ""; }
  bool canBack() const { return count && cursor > 0; }
  bool canForward() const { return count && cursor + 1 < count; }
  void moveBack() {
    if (canBack()) --cursor;
  }
  void moveForward() {
    if (canForward()) ++cursor;
  }
  void erase(std::string_view key) {
    for (size_t i = 0; i < count;) {
      if (key != entries[i].data()) {
        ++i;
        continue;
      }
      for (size_t j = i + 1; j < count; ++j) entries[j - 1] = entries[j];
      --count;
      if (cursor > i || (cursor == i && cursor == count && cursor)) --cursor;
    }
  }
  void visit(std::string_view key, bool replace = false) {
    if (key.size() != 32 || key.find_first_not_of("0123456789abcdef") != std::string_view::npos) return;
    if (key == current()) return;
    if (count && replace) {
      std::memcpy(entries[cursor].data(), key.data(), 32);
      return;
    }
    if (count) count = cursor + 1;
    if (count == CAPACITY) {
      for (size_t i = 1; i < count; ++i) entries[i - 1] = entries[i];
      --count;
    }
    cursor = count++;
    std::memcpy(entries[cursor].data(), key.data(), 32);
    entries[cursor][32] = 0;
  }

 private:
  std::array<std::array<char, 33>, CAPACITY> entries{};
  size_t count = 0;
  size_t cursor = 0;
};
}  // namespace crossloop::web

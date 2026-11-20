#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace crossloop::saved {
constexpr size_t CAPACITY = 32;
enum class Kind : uint8_t { Book = 1, Web = 2 };
struct Entry {
  Kind kind{};
  char title[97]{};
  char target[1025]{};
};
struct Table {
  uint16_t count = 0;
  Entry entries[CAPACITY]{};
};
enum class Result { Saved, Removed, Full, Invalid, StorageError, NoMemory };
bool validEntry(const Entry& entry);
int find(const Table& table, Kind kind, std::string_view target);
bool put(Table& table, Kind kind, std::string_view title, std::string_view target);
bool erase(Table& table, size_t index);
bool load(Table& table);
bool save(const Table& table);
bool contains(Kind kind, const char* target);
Result toggle(Kind kind, const char* title, const char* target);
}  // namespace crossloop::saved

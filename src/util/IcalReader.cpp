#include "IcalReader.h"

#include <algorithm>
#include <cstring>

namespace crossloop::calendar {
bool IcalReader::open(const char* path) {
  close();
  at = size = used = 0;
  first = true;
  failed = false;
  file = Storage.open(path);
  return file && !file.isDirectory() && file.size() <= 2 * 1024 * 1024;
}
int IcalReader::peek() {
  if (at == size) {
    if (!file || file.position() >= file.size()) return -1;
    const int count = file.read(input, sizeof(input));
    if (count <= 0) {
      failed = true;
      return -1;
    }
    at = 0;
    size = static_cast<size_t>(count);
  }
  return input[at];
}
int IcalReader::take() {
  const int value = peek();
  if (value >= 0) ++at;
  return value;
}
LineResult IcalReader::next() {
  used = 0;
  if (first) {
    first = false;
    if (peek() == 0xef && (take() != 0xef || take() != 0xbb || take() != 0xbf)) return LineResult::Invalid;
  }
  while (true) {
    const int value = take();
    if (value < 0) {
      if (failed) return LineResult::StorageError;
      line[used] = 0;
      return used ? LineResult::Line : LineResult::End;
    }
    if (value == '\r' || value == '\n') {
      if (value == '\r' && take() != '\n') return failed ? LineResult::StorageError : LineResult::Invalid;
      const int following = peek();
      if (following == ' ' || following == '\t') {
        take();
        continue;
      }
      if (failed) return LineResult::StorageError;
      line[used] = 0;
      return LineResult::Line;
    }
    if (!value || (value < 32 && value != '\t') || value == 127) return LineResult::Invalid;
    if (used == sizeof(line) - 1) return LineResult::TooLarge;
    line[used++] = static_cast<char>(value);
  }
}
bool same(std::string_view left, std::string_view right) {
  if (left.size() != right.size()) return false;
  for (size_t i = 0; i < left.size(); ++i) {
    auto upper = [](char c) { return c >= 'a' && c <= 'z' ? c - 'a' + 'A' : c; };
    if (upper(left[i]) != upper(right[i])) return false;
  }
  return true;
}
namespace {
bool name(std::string_view value) {
  if (value.empty() || value.size() > 64) return false;
  for (unsigned char c : value) {
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-')) return false;
  }
  return true;
}
}  // namespace
bool property(std::string_view line, Property& result) {
  bool quoted = false;
  size_t colon = std::string_view::npos;
  for (size_t i = 0; i < line.size(); ++i) {
    if (line[i] == '"') quoted = !quoted;
    if (line[i] == ':' && !quoted) {
      colon = i;
      break;
    }
  }
  if (quoted || colon == std::string_view::npos || !colon) return false;
  const auto left = line.substr(0, colon);
  const auto semicolon = left.find(';');
  result.name = left.substr(0, semicolon);
  result.parameters = semicolon == std::string_view::npos ? std::string_view{} : left.substr(semicolon + 1);
  result.value = line.substr(colon + 1);
  if (!name(result.name) || (semicolon != std::string_view::npos && result.parameters.empty())) return false;
  return true;
}
bool parameter(const Property& property, std::string_view wanted, std::string_view& output) {
  auto rest = property.parameters;
  output = {};
  bool found = false;
  unsigned count = 0;
  while (!rest.empty()) {
    if (++count > 16) return false;
    const auto equals = rest.find('=');
    if (equals == std::string_view::npos || !name(rest.substr(0, equals))) return false;
    const auto key = rest.substr(0, equals);
    rest.remove_prefix(equals + 1);
    if (rest.empty()) return false;
    std::string_view value;
    if (rest.front() == '"') {
      const auto quote = rest.find('"', 1);
      if (quote == std::string_view::npos) return false;
      value = rest.substr(1, quote - 1);
      rest.remove_prefix(quote + 1);
      if (!rest.empty() && rest.front() != ';') return false;
    } else {
      const auto semicolon = rest.find(';');
      value = rest.substr(0, semicolon);
      rest.remove_prefix(value.size());
      if (value.find('"') != std::string_view::npos) return false;
    }
    if (value.empty()) return false;
    if (same(key, wanted)) {
      if (found) return false;
      output = value;
      found = true;
    }
    if (!rest.empty()) {
      rest.remove_prefix(1);
      if (rest.empty()) return false;
    }
  }
  return true;
}
bool copyText(std::string_view value, char* destination, size_t capacity, bool truncate) {
  if (!destination || !capacity) return false;
  size_t used = 0;
  bool full = false;
  for (size_t i = 0; i < value.size(); ++i) {
    char c = value[i];
    if (c == '\\') {
      if (++i == value.size()) return false;
      c = value[i];
      if (c == 'n' || c == 'N')
        c = ' ';
      else if (c != '\\' && c != ',' && c != ';')
        return false;
    }
    if (static_cast<unsigned char>(c) < 32) c = ' ';
    if (used + 1 < capacity && !full)
      destination[used++] = c;
    else {
      if (!truncate) return false;
      if (!full) {
        // If the first omitted byte continues a codepoint, remove its partial
        // prefix rather than emitting invalid UTF-8 in a title/location.
        if ((static_cast<unsigned char>(c) & 0xc0) == 0x80) {
          while (used && (static_cast<unsigned char>(destination[used - 1]) & 0xc0) == 0x80) --used;
          if (used) --used;
        }
        full = true;
      }
    }
  }
  destination[used] = 0;
  return true;
}
}  // namespace crossloop::calendar

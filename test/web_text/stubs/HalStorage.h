#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace fake {
inline std::map<std::string, std::vector<uint8_t>> files;
inline int64_t writesLeft = -1;
inline bool failReplace = false;
inline int renameFailure = 0;
inline int renameCalls = 0;
inline bool failClose = false;
inline int shortReadAt = -1;
}  // namespace fake
class HalFile {
 public:
  HalFile() = default;
  explicit HalFile(std::string path) : path(std::move(path)), opened(true) {}
  explicit operator bool() const { return opened && fake::files.contains(path); }
  size_t size() const { return *this ? fake::files.at(path).size() : 0; }
  size_t position() const { return offset; }
  bool isDirectory() const { return false; }
  bool seek(size_t value) {
    if (!*this) return false;
    offset = value;
    return true;
  }
  int available() const { return offset < size(); }
  int read(void* out, size_t length) {
    if (!*this || (fake::shortReadAt >= 0 && offset >= static_cast<size_t>(fake::shortReadAt))) return 0;
    length = std::min(length, size() - std::min(offset, size()));
    memcpy(out, fake::files[path].data() + offset, length);
    offset += length;
    return static_cast<int>(length);
  }
  size_t write(const void* input, size_t length) {
    if (!*this) return 0;
    if (fake::writesLeft >= 0) length = std::min(length, static_cast<size_t>(fake::writesLeft));
    auto& file = fake::files[path];
    if (file.size() < offset + length) file.resize(offset + length);
    memcpy(file.data() + offset, input, length);
    offset += length;
    if (fake::writesLeft >= 0) fake::writesLeft -= length;
    return length;
  }
  bool close() {
    opened = false;
    return !fake::failClose;
  }

 private:
  std::string path;
  size_t offset = 0;
  bool opened = false;
};
class FakeStorage {
 public:
  bool ensureDirectoryExists(const char*) { return true; }
  bool exists(const char* path) { return fake::files.contains(path); }
  HalFile open(const char* path) { return fake::files.contains(path) ? HalFile(path) : HalFile(); }
  bool openFileForWrite(const char*, const std::string& path, HalFile& out) {
    fake::files[path].clear();
    out = HalFile(path);
    return true;
  }
  bool remove(const char* path) { return fake::files.erase(path) > 0; }
  bool rename(const char* source, const char* target) {
    ++fake::renameCalls;
    if ((fake::renameFailure && fake::renameCalls == fake::renameFailure) || fake::failReplace ||
        !fake::files.contains(source) || fake::files.contains(target))
      return false;
    fake::files[target] = std::move(fake::files[source]);
    fake::files.erase(source);
    return true;
  }
  bool replaceFile(const char* source, const char* target) {
    if (!fake::files.contains(source)) return false;
    fake::files.erase(target);
    if (fake::failReplace) return false;
    fake::files[target] = fake::files[source];
    fake::files.erase(source);
    return true;
  }
};
inline FakeStorage Storage;

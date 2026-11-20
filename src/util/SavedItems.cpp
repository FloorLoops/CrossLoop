#include "SavedItems.h"

#include <HalMemory.h>
#include <HalStorage.h>

#include <algorithm>
#include <cstring>
#include <new>

#include "RecoverableFile.h"
#include "network/WebUrl.h"

namespace crossloop::saved {
namespace {
constexpr char PATH[] = "/.crosspoint/crossloop-saved.bin";
constexpr char BACKUP[] = "/.crosspoint/crossloop-saved.bin.bak";
constexpr char TEMP[] = "/.crosspoint/crossloop-saved.bin.part";
constexpr char MAGIC[8] = {'C', 'L', 'S', 'A', 'V', 'E', '1', 0};
constexpr size_t HEADER_BYTES = 16;
static_assert(sizeof(Entry) == 1123);

uint32_t hashBytes(uint32_t hash, const void* source, size_t length) {
  const auto* bytes = static_cast<const uint8_t*>(source);
  for (size_t i = 0; i < length; ++i) hash = (hash ^ bytes[i]) * 16777619u;
  return hash;
}
bool cleanText(const char* text, size_t capacity) {
  const char* end = static_cast<const char*>(memchr(text, 0, capacity));
  if (!end) return false;
  while (text < end) {
    uint32_t value = static_cast<unsigned char>(*text++);
    if (value < 32 || value == 127) return false;
    if (value < 128) continue;
    unsigned extra = value >= 0xc2 && value <= 0xdf   ? 1
                     : value >= 0xe0 && value <= 0xef ? 2
                     : value >= 0xf0 && value <= 0xf4 ? 3
                                                      : 0;
    if (!extra || size_t(end - text) < extra) return false;
    value &= (1u << (6 - extra)) - 1;
    const uint32_t minimum = extra == 1 ? 0x80 : extra == 2 ? 0x800 : 0x10000;
    while (extra--) {
      const unsigned byte = static_cast<unsigned char>(*text++);
      if ((byte & 0xc0) != 0x80) return false;
      value = (value << 6) | (byte & 0x3f);
    }
    if (value < minimum || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff)) return false;
  }
  return true;
}
bool read(const char* path, Table* output) {
  auto file = Storage.open(path);
  uint8_t header[HEADER_BYTES]{};
  if (!file || file.isDirectory() || file.read(header, sizeof(header)) != sizeof(header) ||
      memcmp(header, MAGIC, sizeof(MAGIC)) || header[10] || header[11])
    return false;
  const unsigned count = header[8] | (unsigned(header[9]) << 8);
  if (count > CAPACITY || file.size() != HEADER_BYTES + count * sizeof(Entry)) return false;
  // Validation during publication must reject the same records as loading.
  auto scratch = output ? HalMemory::PsramBuffer{} : HalMemory::allocatePsram(sizeof(Table));
  if (!output) {
    if (!scratch) return false;
    output = new (scratch.get()) Table;
  }
  uint32_t hash = hashBytes(2166136261u, header, 12);
  for (unsigned i = 0; i < count; ++i) {
    auto& entry = output->entries[i];
    if (file.read(&entry, sizeof(entry)) != sizeof(entry) || !validEntry(entry)) return false;
    hash = hashBytes(hash, &entry, sizeof(entry));
    for (unsigned j = 0; j < i; ++j)
      if (output->entries[j].kind == entry.kind && !strcmp(output->entries[j].target, entry.target)) return false;
  }
  const uint32_t expected =
      uint32_t(header[12]) | (uint32_t(header[13]) << 8) | (uint32_t(header[14]) << 16) | (uint32_t(header[15]) << 24);
  if (hash != expected) return false;
  if (output) output->count = static_cast<uint16_t>(count);
  return true;
}
}  // namespace

bool validEntry(const Entry& entry) {
  if (!cleanText(entry.title, sizeof(entry.title)) || !cleanText(entry.target, sizeof(entry.target)) ||
      !entry.title[0] || !entry.target[0])
    return false;
  if (entry.kind == Kind::Web) return web::validUrl(entry.target);
  if (entry.kind != Kind::Book || entry.target[0] != '/') return false;
  const std::string_view path(entry.target);
  return path != "/" && path.find("/../") == path.npos && !path.ends_with("/..") && path.find("/./") == path.npos &&
         !path.ends_with("/.");
}
int find(const Table& table, Kind kind, std::string_view target) {
  if (table.count > CAPACITY) return -1;
  for (unsigned i = 0; i < table.count; ++i)
    if (table.entries[i].kind == kind && target == table.entries[i].target) return static_cast<int>(i);
  return -1;
}
bool put(Table& table, Kind kind, std::string_view title, std::string_view target) {
  if (table.count > CAPACITY || target.empty() || target.size() > 1024 || target.find('\0') != target.npos ||
      title.find('\0') != title.npos)
    return false;
  auto scratch = HalMemory::allocatePsram(sizeof(Entry));
  if (!scratch) return false;
  auto& entry = *new (scratch.get()) Entry;
  entry.kind = kind;
  if (title.empty()) title = target;
  size_t length = std::min(title.size(), sizeof(entry.title) - 1);
  if (length < title.size())
    while (length && (static_cast<unsigned char>(title[length]) & 0xc0) == 0x80) --length;
  memcpy(entry.title, title.data(), length);
  memcpy(entry.target, target.data(), target.size());
  if (!validEntry(entry)) return false;
  const int existing = find(table, kind, target);
  if (existing >= 0)
    table.entries[existing] = entry;
  else {
    if (table.count == CAPACITY) return false;
    table.entries[table.count++] = entry;
  }
  return true;
}
bool erase(Table& table, size_t index) {
  if (table.count > CAPACITY || index >= table.count) return false;
  for (size_t i = index; i + 1 < table.count; ++i) table.entries[i] = table.entries[i + 1];
  table.entries[--table.count] = {};
  return true;
}
bool load(Table& table) {
  table.count = 0;
  if (read(PATH, &table) || read(BACKUP, &table)) return true;
  table.count = 0;
  return !Storage.exists(PATH) && !Storage.exists(BACKUP);
}
bool save(const Table& table) {
  if (table.count > CAPACITY) return false;
  for (unsigned i = 0; i < table.count; ++i) {
    if (!validEntry(table.entries[i]) || find(table, table.entries[i].kind, table.entries[i].target) != int(i))
      return false;
  }
  const bool primaryValid = read(PATH, nullptr);
  if (!primaryValid && !read(BACKUP, nullptr) && (Storage.exists(PATH) || Storage.exists(BACKUP))) return false;
  auto previous = Storage.open(TEMP);
  if (previous && previous.isDirectory()) return false;
  if (previous) previous.close();
  HalFile file;
  if (!Storage.openFileForWrite("SAVED", TEMP, file)) return false;
  uint8_t header[HEADER_BYTES]{};
  memcpy(header, MAGIC, sizeof(MAGIC));
  header[8] = static_cast<uint8_t>(table.count);
  header[9] = static_cast<uint8_t>(table.count >> 8);
  uint32_t hash = hashBytes(2166136261u, header, 12);
  hash = hashBytes(hash, table.entries, table.count * sizeof(Entry));
  for (unsigned i = 0; i < 4; ++i) header[12 + i] = static_cast<uint8_t>(hash >> (8 * i));
  bool ok = file.write(header, sizeof(header)) == sizeof(header);
  const size_t bytes = table.count * sizeof(Entry);
  if (ok && bytes) ok = file.write(table.entries, bytes) == bytes;
  ok = file.close() && ok;
  ok = ok && read(TEMP, nullptr) && publishRecoverableFile(TEMP, PATH, primaryValid);
  if (!ok) Storage.remove(TEMP);
  return ok;
}
bool contains(Kind kind, const char* target) {
  if (!target) return false;
  auto memory = HalMemory::allocatePsram(sizeof(Table));
  if (!memory) return false;
  auto* table = new (memory.get()) Table;
  return load(*table) && find(*table, kind, target) >= 0;
}
Result toggle(Kind kind, const char* title, const char* target) {
  if (!target || !title) return Result::Invalid;
  auto memory = HalMemory::allocatePsram(sizeof(Table));
  if (!memory) return Result::NoMemory;
  auto* table = new (memory.get()) Table;
  if (!load(*table)) return Result::StorageError;
  const int existing = find(*table, kind, target);
  if (existing >= 0) {
    erase(*table, static_cast<size_t>(existing));
    return save(*table) ? Result::Removed : Result::StorageError;
  }
  if (table->count == CAPACITY) return Result::Full;
  if (!put(*table, kind, title, target)) return Result::Invalid;
  return save(*table) ? Result::Saved : Result::StorageError;
}
}  // namespace crossloop::saved

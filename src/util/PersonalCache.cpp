#include "PersonalCache.h"

#include <Arduino.h>
#include <Memory.h>

#include <cstring>
#include <ctime>

#include "RecoverableFile.h"

namespace crossloop::personal {
namespace {
constexpr char DIRECTORY[] = "/.crosspoint/personal";
constexpr char MAGIC[] = "CLDATA01";
constexpr size_t HEADER_SIZE = 24;
constexpr size_t RECORD_SIZE = sizeof(Item) + 4;
uint32_t checksum(const void* data, size_t length) {
  uint32_t value = 2166136261u;
  const auto* bytes = static_cast<const uint8_t*>(data);
  while (length--) value = (value ^ *bytes++) * 16777619u;
  return value;
}
void put32(uint8_t* data, uint32_t value) {
  for (unsigned i = 0; i < 4; ++i) data[i] = static_cast<uint8_t>(value >> (i * 8));
}
uint32_t get32(const uint8_t* data) {
  uint32_t value = 0;
  for (unsigned i = 0; i < 4; ++i) value |= uint32_t(data[i]) << (i * 8);
  return value;
}
template <size_t N>
bool terminated(const char (&text)[N]) {
  return memchr(text, 0, N) != nullptr;
}
}  // namespace
bool validItem(const Item& item) {
  return terminated(item.id) && terminated(item.group) && terminated(item.title) && terminated(item.detail) &&
         terminated(item.target) && terminated(item.when);
}
std::string cachePath(Dataset dataset) {
  const char* names[] = {"projects", "tasks", "feeds", "calendar", "weather", "calendar-preview"};
  const auto index = static_cast<unsigned>(dataset);
  return index < 6 ? std::string(DIRECTORY) + "/" + names[index] + ".bin" : std::string{};
}
bool removeCache(Dataset dataset) {
  const auto path = cachePath(dataset);
  if (path.empty()) return false;
  bool ok = true;
  for (const char* suffix : {"", ".bak", ".part"}) {
    const auto candidate = path + suffix;
    if (Storage.exists(candidate.c_str()) && !Storage.remove(candidate.c_str())) ok = false;
  }
  return ok;
}
void formatSyncTime(uint32_t epoch, char* output, size_t size) {
  if (!size) return;
  output[0] = 0;
  if (epoch < 1735689600u || epoch >= 4102444800u) return;
  const time_t value = epoch;
  tm local{};
  if (localtime_r(&value, &local)) strftime(output, size, "%Y-%m-%d %H:%M", &local);
}
bool CacheReader::openPath(const std::string& path) {
  close();
  file = Storage.open(path.c_str());
  uint8_t header[HEADER_SIZE];
  if (!file || file.isDirectory() || file.read(header, sizeof(header)) != sizeof(header) || memcmp(header, MAGIC, 8) ||
      get32(header + 16) != sizeof(Item) || checksum(header, 20) != get32(header + 20)) {
    close();
    return false;
  }
  count = get32(header + 8);
  syncedAt = get32(header + 12);
  if (count > MAX_RECORDS || file.size() != HEADER_SIZE + count * RECORD_SIZE) {
    close();
    return false;
  }
  if (syncedAt < 1735689600u || syncedAt >= 4102444800u) syncedAt = 0;
  // Validate the complete snapshot once. Never mix a broken primary with rows
  // from a different generation of the backup.
  auto item = makeUniqueNoThrow<Item>();
  if (!item) {
    close();
    return false;
  }
  for (unsigned i = 0; i < count; ++i) {
    if (!read(i, *item)) {
      close();
      return false;
    }
    if ((i & 15) == 15) delay(1);
  }
  return true;
}
bool CacheReader::open(Dataset dataset) {
  const auto path = cachePath(dataset);
  return !path.empty() && (openPath(path) || openPath(path + ".bak"));
}
bool CacheReader::read(unsigned index, Item& item) {
  uint8_t hash[4];
  if (!file || index >= count || !file.seek(HEADER_SIZE + index * RECORD_SIZE) ||
      file.read(reinterpret_cast<uint8_t*>(&item), sizeof(item)) != sizeof(item) ||
      file.read(hash, sizeof(hash)) != sizeof(hash))
    return false;
  return validItem(item) && checksum(&item, sizeof(item)) == get32(hash);
}
bool CacheWriter::begin(Dataset requested) {
  cancel();
  dataset = requested;
  const auto path = cachePath(dataset);
  if (path.empty() || !Storage.ensureDirectoryExists(DIRECTORY)) return false;
  temporary = path + ".part";
  if (!Storage.openFileForWrite("CLDATA", temporary, file)) {
    cancel();
    return false;
  }
  const uint8_t header[HEADER_SIZE]{};
  failed = file.write(header, sizeof(header)) != sizeof(header);
  return !failed;
}
bool CacheWriter::append(const Item& item) {
  if (failed || !file || count >= MAX_RECORDS || !validItem(item)) {
    failed = true;
    return false;
  }
  uint8_t hash[4];
  put32(hash, checksum(&item, sizeof(item)));
  failed = file.write(reinterpret_cast<const uint8_t*>(&item), sizeof(item)) != sizeof(item) ||
           file.write(hash, sizeof(hash)) != sizeof(hash);
  if (!failed) ++count;
  return !failed;
}
bool CacheWriter::commit() {
  if (!file || failed) return false;
  uint8_t header[HEADER_SIZE]{};
  memcpy(header, MAGIC, 8);
  put32(header + 8, count);
  const auto now = time(nullptr);
  put32(header + 12, now >= 1735689600LL && now < 4102444800LL ? static_cast<uint32_t>(now) : 0);
  put32(header + 16, sizeof(Item));
  put32(header + 20, checksum(header, 20));
  if (!file.seek(0) || file.write(header, sizeof(header)) != sizeof(header) || !file.close()) return false;
  CacheReader prior;
  const bool validPrimary = prior.openPath(cachePath(dataset));
  prior.close();
  if (!publishRecoverableFile(temporary, cachePath(dataset), validPrimary)) return false;
  temporary.clear();
  return true;
}
void CacheWriter::cancel() {
  file.close();
  if (!temporary.empty()) Storage.remove(temporary.c_str());
  temporary.clear();
  count = 0;
  failed = false;
}
}  // namespace crossloop::personal

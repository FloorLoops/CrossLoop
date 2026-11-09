#include "BookDetails.h"

#include <HalStorage.h>
#include <Memory.h>

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string_view>

#include "RecoverableFile.h"

namespace crossloop::books {
namespace {
constexpr unsigned HEADER_SIZE = 12;
constexpr char MAGIC[] = "CLMETA1";
uint32_t checksum(const Details& d) {
  uint32_t hash = 2166136261u;
  const auto* bytes = reinterpret_cast<const uint8_t*>(&d);
  for (size_t i = 0; i < sizeof(d); ++i) {
    hash ^= bytes[i];
    hash *= 16777619u;
  }
  return hash;
}
bool validText(const char* value, size_t capacity) {
  const auto* end = static_cast<const char*>(memchr(value, 0, capacity));
  if (!end) return false;
  while (value < end) {
    const auto first = static_cast<unsigned char>(*value++);
    if (first < 32 || first == 127) return false;
    if (first < 128) continue;
    unsigned count = 0, code = 0, minimum = 0;
    if (first >= 0xc2 && first <= 0xdf) {
      count = 1;
      code = first & 31;
      minimum = 0x80;
    } else if (first >= 0xe0 && first <= 0xef) {
      count = 2;
      code = first & 15;
      minimum = 0x800;
    } else if (first >= 0xf0 && first <= 0xf4) {
      count = 3;
      code = first & 7;
      minimum = 0x10000;
    } else
      return false;
    if (size_t(end - value) < count) return false;
    while (count--) {
      const auto next = static_cast<unsigned char>(*value++);
      if ((next & 0xc0) != 0x80) return false;
      code = (code << 6) | (next & 63);
    }
    if (code < minimum || code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff)) return false;
  }
  return true;
}
bool read(const std::string& path, Details& details) {
  auto file = Storage.open(path.c_str());
  uint8_t header[HEADER_SIZE]{};
  if (!file || file.isDirectory() || file.size() != HEADER_SIZE + sizeof(details) ||
      file.read(header, sizeof(header)) != sizeof(header) || memcmp(header, MAGIC, 8) ||
      file.read(&details, sizeof(details)) != sizeof(details) || !validDetails(details))
    return false;
  const uint32_t expected =
      uint32_t(header[8]) | (uint32_t(header[9]) << 8) | (uint32_t(header[10]) << 16) | (uint32_t(header[11]) << 24);
  return expected == checksum(details);
}
}  // namespace
bool validDetails(const Details& d) {
  if (!validText(d.title, sizeof(d.title)) || !validText(d.author, sizeof(d.author)) ||
      !validText(d.genre, sizeof(d.genre)) || !validText(d.series, sizeof(d.series)) ||
      !validText(d.seriesNumber, sizeof(d.seriesNumber)))
    return false;
  if (!d.seriesNumber[0]) return true;
  bool point = false, digit = false;
  for (char c : std::string_view(d.seriesNumber)) {
    if (c == '.' && !point)
      point = true;
    else if (c >= '0' && c <= '9')
      digit = true;
    else
      return false;
  }
  return digit && strtod(d.seriesNumber, nullptr) <= 1000000;
}
bool emptyDetails(const Details& d) {
  return !d.title[0] && !d.author[0] && !d.genre[0] && !d.series[0] && !d.seriesNumber[0];
}
DetailsResult loadDetails(const std::string& bookPath, Details& details) {
  const auto primary = bookPath + DETAILS_SUFFIXES[0];
  const auto backup = bookPath + DETAILS_SUFFIXES[1];
  if (read(primary, details) || read(backup, details)) return DetailsResult::Ok;
  memset(&details, 0, sizeof(details));
  return Storage.exists(primary.c_str()) || Storage.exists(backup.c_str()) ? DetailsResult::Error
                                                                           : DetailsResult::Missing;
}
bool saveDetails(const std::string& bookPath, const Details& details) {
  if (bookPath.empty() || bookPath.front() != '/' || !validDetails(details)) return false;
  auto book = Storage.open(bookPath.c_str());
  if (!book || book.isDirectory()) return false;
  book.close();
  const auto primary = bookPath + DETAILS_SUFFIXES[0];
  const auto temporary = bookPath + DETAILS_SUFFIXES[2];
  auto checked = makeUniqueNoThrow<Details>();
  if (!checked || loadDetails(bookPath, *checked) == DetailsResult::Error) return false;
  const bool primaryValid = read(primary, *checked);
  auto existing = Storage.open(temporary.c_str());
  if (existing && existing.isDirectory()) return false;
  existing.close();
  HalFile file;
  if (!Storage.openFileForWrite("DETAILS", temporary, file)) return false;
  uint8_t header[HEADER_SIZE]{};
  memcpy(header, MAGIC, 8);
  const auto hash = checksum(details);
  for (unsigned i = 0; i < 4; ++i) header[8 + i] = static_cast<uint8_t>(hash >> (8 * i));
  bool ok =
      file.write(header, sizeof(header)) == sizeof(header) && file.write(&details, sizeof(details)) == sizeof(details);
  ok = file.close() && ok;
  ok = ok && read(temporary, *checked) && publishRecoverableFile(temporary, primary, primaryValid);
  if (!ok) Storage.remove(temporary.c_str());
  return ok;
}
}  // namespace crossloop::books

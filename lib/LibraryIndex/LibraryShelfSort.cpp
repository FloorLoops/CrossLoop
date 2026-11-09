#include "LibraryShelfSort.h"

#include <Arduino.h>
#include <HalStorage.h>
#include <Memory.h>

#include <algorithm>
#include <cmath>
#include <cstring>

#include "LibraryFormat.h"

namespace library {
namespace {
constexpr char FIRST[] = "/.crosspoint/library-shelf.a";
constexpr char SECOND[] = "/.crosspoint/library-shelf.b";
constexpr unsigned RUN = 32;
struct Workspace {
  ShelfKey run[RUN];
  ShelfKey left, right;
};
bool less(const ShelfKey& a, const ShelfKey& b) {
  if (!a.name[0] != !b.name[0]) return a.name[0] != 0;
  const int names = strcmp(a.name, b.name);
  if (names) return names < 0;
  if (a.number != b.number) return a.number < b.number;
  return a.ordinal < b.ordinal;
}
bool readAt(HalFile& file, unsigned ordinal, ShelfKey& key) {
  return file.seek(ordinal * sizeof(ShelfKey)) && file.read(&key, sizeof(key)) == sizeof(key) &&
         memchr(key.name, 0, sizeof(key.name)) && !std::isnan(key.number) &&
         key.ordinal < file.size() / sizeof(ShelfKey);
}
}  // namespace
bool sortShelf(uint16_t count, ShelfSource source, ShelfOutput output, void* context) {
  if (count > CLIX_MAX_RECORDS || !source || !output) return false;
  if (!count) return true;
  auto work = makeUniqueNoThrow<Workspace>();
  if (!work) return false;
  for (const auto* path : {FIRST, SECOND}) {
    auto file = Storage.open(path);
    if (file && file.isDirectory()) return false;
  }
  struct Cleanup {
    ~Cleanup() {
      Storage.remove(FIRST);
      Storage.remove(SECOND);
    }
  } cleanup;
  HalFile file;
  if (!Storage.openFileForWrite("SHELF", FIRST, file)) return false;
  for (unsigned base = 0; base < count; base += RUN) {
    const unsigned n = std::min<unsigned>(RUN, count - base);
    for (unsigned i = 0; i < n; ++i) {
      auto& key = work->run[i];
      key = {};
      if (!source(context, static_cast<uint16_t>(base + i), key) || !memchr(key.name, 0, sizeof(key.name)) ||
          std::isnan(key.number) || key.ordinal >= count)
        return false;
    }
    std::sort(work->run, work->run + n, less);
    if (file.write(work->run, n * sizeof(ShelfKey)) != n * sizeof(ShelfKey)) return false;
    delay(1);
  }
  if (!file.close()) return false;
  const char* inputPath = FIRST;
  const char* outputPath = SECOND;
  for (unsigned run = RUN; run < count; run *= 2) {
    auto input = Storage.open(inputPath);
    HalFile out;
    if (!input || input.size() != count * sizeof(ShelfKey) || !Storage.openFileForWrite("SHELF", outputPath, out))
      return false;
    for (unsigned base = 0; base < count; base += run * 2) {
      unsigned left = base, right = std::min<unsigned>(base + run, count);
      const unsigned leftEnd = right, rightEnd = std::min<unsigned>(base + run * 2, count);
      bool haveLeft = false, haveRight = false;
      while (left < leftEnd || right < rightEnd) {
        if (left < leftEnd && !haveLeft) {
          if (!readAt(input, left, work->left)) return false;
          haveLeft = true;
        }
        if (right < rightEnd && !haveRight) {
          if (!readAt(input, right, work->right)) return false;
          haveRight = true;
        }
        const bool takeLeft = haveLeft && (!haveRight || less(work->left, work->right));
        const auto& next = takeLeft ? work->left : work->right;
        if (out.write(&next, sizeof(next)) != sizeof(next)) return false;
        if (takeLeft) {
          ++left;
          haveLeft = false;
        } else {
          ++right;
          haveRight = false;
        }
        if (((left + right) & 31) == 0) delay(1);
      }
    }
    input.close();
    if (!out.close()) return false;
    std::swap(inputPath, outputPath);
  }
  auto sorted = Storage.open(inputPath);
  if (!sorted || sorted.size() != count * sizeof(ShelfKey)) return false;
  for (unsigned i = 0; i < count; ++i) {
    if (!readAt(sorted, i, work->left) || !output(context, work->left.ordinal)) return false;
    if ((i & 31) == 0) delay(1);
  }
  return true;
}
}  // namespace library

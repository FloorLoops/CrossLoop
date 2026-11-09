#pragma once

#include <HalStorage.h>

#include <cstddef>
#include <cstdint>
#include <string>

namespace crossloop::personal {
enum class Dataset : uint8_t { Projects, Tasks, Feeds, Calendar, Weather, CalendarPreview };
constexpr unsigned MAX_RECORDS = 256;
// Fixed on-disk v1 record, byte fields only: no compiler padding or pointers.
struct Item {
  char id[129]{};
  char group[129]{};
  char title[193]{};
  char detail[129]{};
  char target[1025]{};
  char when[49]{};
  uint8_t flags = 0;
};
static_assert(sizeof(Item) == 1655);
enum : uint8_t { AllDay = 1, Completed = 2, Recurring = 4 };
bool validItem(const Item& item);
std::string cachePath(Dataset dataset);
bool removeCache(Dataset dataset);
void formatSyncTime(uint32_t epoch, char* output, size_t size);

class CacheReader {
 public:
  bool open(Dataset dataset);
  void close() {
    file.close();
    count = 0;
    syncedAt = 0;
  }
  bool read(unsigned index, Item& item);
  unsigned size() const { return count; }
  uint32_t timestamp() const { return syncedAt; }

 private:
  friend class CacheWriter;
  bool openPath(const std::string& path);
  HalFile file;
  unsigned count = 0;
  uint32_t syncedAt = 0;
};

// Stream a complete new snapshot to .part, then preserve the prior snapshot.
// Failed or cancelled refreshes never publish a partial set of lists/tasks.
class CacheWriter {
 public:
  ~CacheWriter() { cancel(); }
  bool begin(Dataset dataset);
  bool append(const Item& item);
  bool commit();
  void cancel();
  unsigned size() const { return count; }

 private:
  Dataset dataset = Dataset::Tasks;
  HalFile file;
  std::string temporary;
  unsigned count = 0;
  bool failed = false;
};
}  // namespace crossloop::personal

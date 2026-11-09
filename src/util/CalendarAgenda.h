#pragma once

#include "PersonalCache.h"

namespace crossloop::calendar {
// Own with the activity, off the task stack. The index stores ordinals/times,
// not 256 full events; the one item buffer is reused for every SD read.
class Agenda {
 public:
  bool open();
  void close() {
    reader.close();
    count = 0;
  }
  unsigned size() const { return count; }
  uint32_t timestamp() const { return reader.timestamp(); }
  int64_t windowEnd() const { return through; }
  const char* name() const { return title; }
  bool read(unsigned index, personal::Item& item);

 private:
  struct Entry {
    int64_t start = 0;
    uint16_t ordinal = 0;
  };
  personal::CacheReader reader;
  personal::Item scratch;
  Entry entries[personal::MAX_RECORDS]{};
  char title[193]{};
  unsigned count = 0;
  int64_t through = 0;
};
// A small, separate sleep snapshot avoids validating/scanning the full agenda
// during sleep. It retains up to 12 current/upcoming events from the last sync.
bool prepareCalendarPreview(int64_t now);
}  // namespace crossloop::calendar

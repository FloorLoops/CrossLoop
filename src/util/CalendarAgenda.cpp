#include "CalendarAgenda.h"

#include <Memory.h>

#include <algorithm>
#include <cstring>

#include "CalendarImport.h"

namespace crossloop::calendar {
bool Agenda::open() {
  close();
  title[0] = 0;
  through = 0;
  int64_t from = 0;
  if (!reader.open(personal::Dataset::Calendar) || !reader.size() || !reader.read(0, scratch) ||
      strcmp(scratch.id, "crossloop-calendar-v1") || scratch.flags || !eventSpan(scratch, from, through) ||
      from < 1735689600 || through - from > 31LL * 86400) {
    close();
    return false;
  }
  snprintf(title, sizeof(title), "%s", scratch.title);
  for (unsigned i = 1; i < reader.size(); ++i) {
    int64_t start = 0, end = 0;
    if (!reader.read(i, scratch) || !eventSpan(scratch, start, end)) {
      close();
      return false;
    }
    entries[count++] = {start, static_cast<uint16_t>(i)};
  }
  std::sort(entries, entries + count, [](const Entry& a, const Entry& b) {
    return a.start != b.start ? a.start < b.start : a.ordinal < b.ordinal;
  });
  return true;
}
bool Agenda::read(unsigned index, personal::Item& item) {
  return index < count && reader.read(entries[index].ordinal, item);
}
bool prepareCalendarPreview(int64_t now) {
  if (now < 1735689600 || now >= 4102444800LL) return false;
  auto agenda = makeUniqueNoThrow<Agenda>();
  auto item = makeUniqueNoThrow<personal::Item>();
  personal::CacheWriter writer;
  if (!agenda || !item || !agenda->open() || now > agenda->windowEnd() ||
      !writer.begin(personal::Dataset::CalendarPreview))
    return false;
  snprintf(item->id, sizeof(item->id), "crossloop-calendar-preview-v1");
  snprintf(item->title, sizeof(item->title), "%s", agenda->name());
  // Keep the source sync time distinct from the time this preview was built.
  snprintf(item->when, sizeof(item->when), "%lu/%lld", static_cast<unsigned long>(agenda->timestamp()),
           static_cast<long long>(agenda->windowEnd()));
  if (!writer.append(*item)) return false;
  unsigned kept = 0;
  for (unsigned i = 0; i < agenda->size() && kept < 12; ++i) {
    int64_t start = 0, end = 0;
    if (!agenda->read(i, *item) || !eventSpan(*item, start, end)) return false;
    if (end < now || (end == now && start != end)) continue;
    if (!writer.append(*item)) return false;
    ++kept;
  }
  return writer.commit();
}
}  // namespace crossloop::calendar

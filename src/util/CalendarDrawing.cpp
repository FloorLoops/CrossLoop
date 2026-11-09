#include "CalendarDrawing.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Memory.h>

#include <cstring>
#include <ctime>

#include "CalendarImport.h"
#include "Timezones.h"
#include "fontIds.h"

namespace crossloop::calendar {
void formatEventWhen(const personal::Item& item, char* output, size_t size) {
  if (!size) return;
  output[0] = 0;
  int64_t start = 0, end = 0;
  if (!eventSpan(item, start, end)) return;
  const bool allDay = item.flags & personal::AllDay;
  // An all-day DTEND is exclusive; show the last included civil date.
  const time_t first = static_cast<time_t>(start), last = static_cast<time_t>(allDay ? end - 1 : end);
  tm a{}, b{};
  if (!localtime_r(&first, &a) || !localtime_r(&last, &b)) return;
  char left[24]{}, right[24]{};
  strftime(left, sizeof(left), allDay ? "%Y-%m-%d" : "%m-%d %H:%M", &a);
  strftime(right, sizeof(right), allDay ? "%Y-%m-%d" : "%m-%d %H:%M", &b);
  if (allDay) {
    if (!strcmp(left, right))
      snprintf(output, size, "%s | %s", left, tr(STR_CALENDAR_ALL_DAY));
    else
      snprintf(output, size, "%s - %s | %s", left, right, tr(STR_CALENDAR_ALL_DAY));
  } else if (start == end)
    snprintf(output, size, "%s", left);
  else
    snprintf(output, size, "%s - %s", left, right);
}
namespace {
struct Preview {
  personal::CacheReader reader;
  personal::Item item;
  char when[96]{};
};
void fitted(GfxRenderer& renderer, char* text, int x, int y, int width) {
  size_t length = strlen(text);
  while (length && renderer.getTextWidth(UI_10_FONT_ID, text) > width) {
    do {
      --length;
    } while (length && (static_cast<unsigned char>(text[length]) & 0xc0) == 0x80);
    text[length] = 0;
  }
  renderer.drawText(UI_10_FONT_ID, x, y, text);
}
}  // namespace
void drawCalendarPreview(GfxRenderer& renderer, int x, int y, int width, int height) {
  if (height < 100) return;
  renderer.drawText(UI_12_FONT_ID, x, y, tr(STR_CALENDAR), true, EpdFontFamily::BOLD);
  auto preview = makeUniqueNoThrow<Preview>();
  int64_t sourceTime = 0, through = 0;
  const int64_t now = static_cast<int64_t>(time(nullptr));
  if (!preview || !preview->reader.open(personal::Dataset::CalendarPreview) || preview->reader.size() > 13 ||
      !preview->reader.read(0, preview->item) || strcmp(preview->item.id, "crossloop-calendar-preview-v1") ||
      preview->item.flags || !eventSpan(preview->item, sourceTime, through)) {
    renderer.drawText(UI_10_FONT_ID, x, y + 40, tr(STR_CALENDAR_NO_CACHE));
    return;
  }
  if (now < 1735689600 || now >= 4102444800LL) {
    renderer.drawText(UI_10_FONT_ID, x, y + 40, tr(STR_CALENDAR_CLOCK));
    return;
  }
  unsigned shown = 0;
  int at = y + 40;
  const int footer = y + height - 56;
  if (now < through) {
    for (unsigned i = 1; i < preview->reader.size() && at + 52 <= footer; ++i) {
      int64_t start = 0, end = 0;
      if (!preview->reader.read(i, preview->item) || !eventSpan(preview->item, start, end)) break;
      if (end < now || (end == now && start != end)) continue;
      if (!preview->item.title[0])
        snprintf(preview->item.title, sizeof(preview->item.title), "%s", tr(STR_CALENDAR_UNTITLED));
      fitted(renderer, preview->item.title, x, at, width);
      formatEventWhen(preview->item, preview->when, sizeof(preview->when));
      fitted(renderer, preview->when, x, at + 26, width);
      at += 60;
      ++shown;
    }
  }
  if (!shown) renderer.drawText(UI_10_FONT_ID, x, at, tr(STR_CALENDAR_PREVIEW_EMPTY));
  personal::formatSyncTime(static_cast<uint32_t>(sourceTime), preview->when, sizeof(preview->when));
  snprintf(preview->item.title, sizeof(preview->item.title), tr(STR_PERSONAL_SNAPSHOT_TIME),
           preview->when[0] ? preview->when : "?");
  fitted(renderer, preview->item.title, x, footer, width);
  snprintf(preview->item.title, sizeof(preview->item.title), "%s", timezones::table()[timezones::activeIndex()].name);
  fitted(renderer, preview->item.title, x, footer + 26, width);
}
}  // namespace crossloop::calendar

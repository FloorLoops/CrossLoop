#include "PersonalSnapshot.h"

#include <GfxRenderer.h>
#include <HalClock.h>
#include <I18n.h>
#include <Memory.h>

#include <algorithm>
#include <cstring>
#include <ctime>

#include "DailyCardContent.h"
#include "DailyCardPolicy.h"
#include "PersonalCache.h"
#include "fontIds.h"

namespace crossloop::personal {
namespace {
int drawTitle(GfxRenderer& renderer, char* text, int x, int y, int width, int lineHeight, unsigned maxLines = 2) {
  for (unsigned line = 0; line < maxLines && *text; ++line) {
    char* end = text;
    char* space = nullptr;
    while (*end) {
      char* next = end + 1;
      while ((static_cast<unsigned char>(*next) & 0xc0) == 0x80) ++next;
      const char saved = *next;
      *next = 0;
      const bool fits = renderer.getTextWidth(UI_12_FONT_ID, text) <= width;
      *next = saved;
      if (!fits && end != text) break;
      if (*end == ' ') space = end;
      end = next;
      if (!fits) break;
    }
    if (*end && space) end = space;
    const char saved = *end;
    *end = 0;
    renderer.drawText(UI_12_FONT_ID, x, y, text);
    *end = saved;
    y += lineHeight;
    text = end;
    while (*text == ' ') ++text;
  }
  return y;
}
}  // namespace
void drawDailySnapshot(GfxRenderer& renderer, bool word) {
  renderer.clearScreen();
  constexpr int margin = 32;
  const int width = renderer.getScreenWidth(), height = renderer.getScreenHeight();
  renderer.drawText(UI_12_FONT_ID, margin, 40, word ? tr(STR_PERSONAL_SLEEP_WORD) : tr(STR_PERSONAL_SLEEP_AFFIRMATION),
                    true, EpdFontFamily::BOLD);
  renderer.drawLine(margin, 84, width - margin, 84);
  tm local{};
  const bool clockValid = halClock.localTime(local) && local.tm_year >= 125 && local.tm_year < 200;
  const size_t choices = word ? DAILY_WORDS.size() : DAILY_AFFIRMATIONS.size();
  const size_t index = clockValid ? dailyCardIndex(local.tm_year + 1900, local.tm_mon + 1, local.tm_mday, choices) : 0;
  // One reusable 512-byte buffer for translated content, released before sleep.
  auto text = makeUniqueNoThrow<char[]>(512);
  if (!text) {
    renderer.drawText(UI_10_FONT_ID, margin, 140, tr(STR_MEMORY_ERROR));
    return;
  }
  const auto copy = [&](StrId id) {
    const char* source = I18N.get(id);
    const size_t length = strlen(source);
    size_t count = std::min<size_t>(length, 511);
    while (count && count < length && (static_cast<unsigned char>(source[count]) & 0xc0) == 0x80) --count;
    memcpy(text.get(), source, count);
    text[count] = 0;
  };
  const int lineHeight = renderer.getLineHeight(UI_12_FONT_ID) + 10;
  copy(word ? DAILY_WORDS[index].word : DAILY_AFFIRMATIONS[index]);
  const int bottom = drawTitle(renderer, text.get(), margin, height / 3, width - 2 * margin, lineHeight, 5);
  if (word) {
    copy(DAILY_WORDS[index].meaning);
    const int top = bottom + 24;
    const unsigned available = std::max(0, (height - 96 - top) / lineHeight);
    drawTitle(renderer, text.get(), margin, top, width - 2 * margin, lineHeight, available);
  }
  char date[24]{};
  if (clockValid) strftime(date, sizeof(date), "%Y-%m-%d", &local);
  renderer.drawLine(margin, height - 80, width - margin, height - 80);
  renderer.drawText(UI_10_FONT_ID, margin, height - 58, clockValid ? date : tr(STR_DAILY_DATE_UNKNOWN));
}

void drawTickTickSnapshot(GfxRenderer& renderer) {
  renderer.clearScreen();
  const int width = renderer.getScreenWidth();
  const int height = renderer.getScreenHeight();
  constexpr int margin = 24;
  renderer.drawText(UI_12_FONT_ID, margin, 32, tr(STR_PERSONAL_SLEEP_TICKTICK), true, EpdFontFamily::BOLD);
  renderer.drawLine(margin, 74, width - margin, 74);
  CacheReader reader;
  auto item = makeUniqueNoThrow<Item>();
  if (!item || !reader.open(Dataset::Tasks)) {
    renderer.drawText(UI_10_FONT_ID, margin, 104, tr(STR_TICK_SLEEP_EMPTY));
    return;
  }
  int y = 102;
  const int lineHeight = renderer.getLineHeight(UI_12_FONT_ID) + 4;
  if (!reader.size()) renderer.drawText(UI_10_FONT_ID, margin, y, tr(STR_TICK_EMPTY));
  for (unsigned index = 0; index < reader.size() && y + 3 * lineHeight < height - 72; ++index) {
    if (!reader.read(index, *item)) break;
    renderer.drawRect(margin, y + 5, 14, 14);
    // Two bounded title lines; the app retains the full 192-byte title preview.
    y = drawTitle(renderer, item->title, margin + 28, y, width - 2 * margin - 28, lineHeight);
    if (item->when[0]) {
      // Preserve the provider's due date without inventing a timezone offset.
      char date[11]{};
      snprintf(date, sizeof(date), "%.10s", item->when);
      renderer.drawText(UI_10_FONT_ID, margin + 28, y, date);
      y += renderer.getLineHeight(UI_10_FONT_ID) + 4;
    }
    y += 18;
  }
  char timestamp[32]{};
  formatSyncTime(reader.timestamp(), timestamp, sizeof(timestamp));
  char footer[96]{};
  if (timestamp[0])
    snprintf(footer, sizeof(footer), tr(STR_PERSONAL_SNAPSHOT_TIME), timestamp);
  else
    snprintf(footer, sizeof(footer), "%s", tr(STR_TICK_CACHED));
  renderer.drawLine(margin, height - 64, width - margin, height - 64);
  renderer.drawText(UI_10_FONT_ID, margin, height - 48, footer);
}
}  // namespace crossloop::personal

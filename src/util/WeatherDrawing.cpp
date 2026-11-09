#include "WeatherDrawing.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Memory.h>

#include <cstring>

#include "CalendarDrawing.h"
#include "PersonalCache.h"
#include "fontIds.h"

namespace crossloop::personal {
namespace {
const char* condition(uint8_t code) {
  if (code <= 1) return tr(STR_WEATHER_CLEAR);
  if (code <= 3) return tr(STR_WEATHER_CLOUDY);
  if (code <= 48) return tr(STR_WEATHER_FOG);
  if (code >= 95) return tr(STR_WEATHER_STORM);
  if ((code >= 71 && code <= 77) || code == 85 || code == 86) return tr(STR_WEATHER_SNOW);
  return tr(STR_WEATHER_RAIN);
}
void circle(GfxRenderer& r, int x, int y, int radius) {
  for (int dx : {-1, 1})
    for (int dy : {-1, 1}) r.drawArc(radius, x, y, dx, dy, 2, true);
}
void icon(GfxRenderer& r, int x, int y, uint8_t code, bool daylight) {
  if (code <= 2) {
    circle(r, x + 30, y + 26, 14);
    if (daylight) {
      constexpr int8_t rays[][4] = {{30, 1, 30, 7},  {30, 45, 30, 51}, {5, 26, 11, 26},  {49, 26, 55, 26},
                                    {12, 8, 16, 12}, {44, 40, 48, 44}, {12, 44, 16, 40}, {44, 12, 48, 8}};
      for (const auto& a : rays) r.drawLine(x + a[0], y + a[1], x + a[2], y + a[3], 2, true);
    } else {
      r.fillRoundedRect(x + 28, y + 10, 24, 28, 12, Color::White);
    }
    if (code < 2) return;
  }
  r.fillRoundedRect(x + 8, y + 30, 56, 28, 12, Color::White);
  r.drawRoundedRect(x + 8, y + 30, 56, 28, 2, 12, true);
  r.fillRoundedRect(x + 24, y + 21, 28, 30, 13, Color::White);
  r.drawArc(14, x + 38, y + 35, -1, -1, 2, true);
  r.drawArc(14, x + 38, y + 35, 1, -1, 2, true);
  if (code == 45 || code == 48) {
    r.drawLine(x + 8, y + 65, x + 62, y + 65, 2, true);
    r.drawLine(x + 17, y + 72, x + 55, y + 72, 2, true);
  } else if (code >= 95) {
    r.drawLine(x + 39, y + 59, x + 28, y + 70, 3, true);
    r.drawLine(x + 28, y + 70, x + 41, y + 70, 3, true);
    r.drawLine(x + 41, y + 70, x + 31, y + 83, 3, true);
  } else if ((code >= 71 && code <= 77) || code == 85 || code == 86) {
    for (int offset : {18, 46}) {
      r.drawLine(x + offset, y + 64, x + offset, y + 77, 2, true);
      r.drawLine(x + offset - 5, y + 67, x + offset + 5, y + 74, 2, true);
      r.drawLine(x + offset - 5, y + 74, x + offset + 5, y + 67, 2, true);
    }
  } else if (code >= 51) {
    for (int offset : {20, 36, 52}) r.drawLine(x + offset, y + 64, x + offset - 4, y + 77, 2, true);
  }
}
}  // namespace
void drawWeatherPanel(GfxRenderer& r, const WeatherData& d, int x, int y, int width) {
  char line[128];
  const char* unit = d.fahrenheit ? "F" : "C";
  const bool wide = width >= 640;
  const int currentWidth = wide ? width / 2 - 12 : width;
  snprintf(line, sizeof(line), "%s", d.place);
  while (r.getTextWidth(UI_12_FONT_ID, line) > currentWidth) {
    size_t length = strlen(line);
    if (!length) break;
    do {
      --length;
    } while (length && (static_cast<unsigned char>(line[length]) & 0xc0) == 0x80);
    line[length] = 0;
  }
  r.drawText(UI_12_FONT_ID, x, y, line, true, EpdFontFamily::BOLD);
  icon(r, x, y + 36, d.code, d.daylight);
  snprintf(line, sizeof(line), "%.1f %s", d.temperature, unit);
  r.drawText(UI_12_FONT_ID, x + 90, y + 42, line, true, EpdFontFamily::BOLD);
  r.drawText(UI_10_FONT_ID, x + 90, y + 76, condition(d.code));
  snprintf(line, sizeof(line), tr(STR_WEATHER_FEELS), d.feelsLike, unit);
  r.drawText(UI_10_FONT_ID, x + 90, y + 105, line);
  snprintf(line, sizeof(line), tr(STR_WEATHER_OBSERVED), d.observed);
  r.drawText(UI_10_FONT_ID, x, y + (wide ? 152 : 143), line);
  if (!wide) r.drawLine(x, y + 178, x + width, y + 178);
  const int forecastX = wide ? x + width / 2 : x;
  const int forecastY = wide ? y + 10 : y + 191;
  const int forecastWidth = wide ? width / 2 : width;
  for (unsigned i = 0; i < 3; ++i) {
    const int column = forecastX + (forecastWidth * i) / 3;
    icon(r, column + 10, forecastY, d.days[i].code, true);
    r.drawText(UI_10_FONT_ID, column + 4, forecastY + 88, d.days[i].date + 5);
    snprintf(line, sizeof(line), "%.0f / %.0f %s", d.days[i].high, d.days[i].low, unit);
    r.drawText(UI_10_FONT_ID, column + 4, forecastY + 117, line);
  }
  r.drawText(UI_10_FONT_ID, x, y + (wide ? 182 : 342), tr(STR_WEATHER_CREDIT));
}
void drawTodaySnapshot(GfxRenderer& renderer) {
  renderer.clearScreen();
  constexpr int margin = 24;
  auto weather = makeUniqueNoThrow<WeatherData>();
  renderer.drawText(UI_12_FONT_ID, margin, 32, tr(STR_PERSONAL_TODAY), true, EpdFontFamily::BOLD);
  int calendarY = 156;
  if (!weather || !loadWeather(*weather)) {
    renderer.drawText(UI_10_FONT_ID, margin, 104, tr(STR_WEATHER_NO_CACHE));
  } else {
    drawWeatherPanel(renderer, *weather, margin, 82, renderer.getScreenWidth() - 2 * margin);
    char timestamp[32]{};
    formatSyncTime(weather->syncedAt, timestamp, sizeof(timestamp));
    char footer[96]{};
    snprintf(footer, sizeof(footer), tr(STR_PERSONAL_SNAPSHOT_TIME), timestamp[0] ? timestamp : "?");
    renderer.drawText(UI_10_FONT_ID, margin, 450, footer);
    calendarY = 490;
  }
  weather.reset();
  renderer.drawLine(margin, calendarY - 10, renderer.getScreenWidth() - margin, calendarY - 10);
  calendar::drawCalendarPreview(renderer, margin, calendarY, renderer.getScreenWidth() - 2 * margin,
                                renderer.getScreenHeight() - calendarY - 20);
}
}  // namespace crossloop::personal

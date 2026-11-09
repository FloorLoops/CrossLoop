#include "WeatherData.h"

#include <Memory.h>
#include <StreamingJsonParser.h>

#include <cmath>
#include <cstdlib>
#include <cstring>

#include "JsonStructure.h"
#include "PersonalCache.h"

namespace crossloop::personal {
bool validWeatherCode(int code) {
  switch (code) {
    case 0:
    case 1:
    case 2:
    case 3:
    case 45:
    case 48:
    case 51:
    case 53:
    case 55:
    case 56:
    case 57:
    case 61:
    case 63:
    case 65:
    case 66:
    case 67:
    case 71:
    case 73:
    case 75:
    case 77:
    case 80:
    case 81:
    case 82:
    case 85:
    case 86:
    case 95:
    case 96:
    case 97:
    case 99:
      return true;
    default:
      return false;
  }
}
namespace {
enum class Field : uint8_t { None, Time, Temperature, Feels, Code, Daylight, Low, High };
enum class Scope { None, Current, Daily, Units };
bool date(const char* text, size_t length, bool timestamp) {
  if (length != (timestamp ? 16u : 10u) || text[4] != '-' || text[7] != '-') return false;
  for (size_t i = 0; i < 10; ++i)
    if (i != 4 && i != 7 && (text[i] < '0' || text[i] > '9')) return false;
  const int year = (text[0] - '0') * 1000 + (text[1] - '0') * 100 + (text[2] - '0') * 10 + text[3] - '0';
  const int month = (text[5] - '0') * 10 + text[6] - '0';
  const int day = (text[8] - '0') * 10 + text[9] - '0';
  constexpr int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (year < 2025 || year >= 2100 || month < 1 || month > 12 || day < 1 ||
      day > days[month - 1] + (month == 2 && year % 4 == 0))
    return false;
  if (!timestamp) return true;
  if (text[10] != 'T' || text[13] != ':') return false;
  for (unsigned i : {11u, 12u, 14u, 15u})
    if (text[i] < '0' || text[i] > '9') return false;
  return (text[11] - '0') * 10 + text[12] - '0' < 24 && (text[14] - '0') * 10 + text[15] - '0' < 60;
}
bool temperature(float value) { return std::isfinite(value) && value >= -200 && value <= 200; }
bool numeric(const char* text, size_t length, float& value) {
  if (!length || length > 31 || memchr(text, 0, length)) return false;
  char copy[32];
  memcpy(copy, text, length);
  copy[length] = 0;
  char* end = nullptr;
  value = strtof(copy, &end);
  return end == copy + length && temperature(value);
}
bool valid(const WeatherData& data) {
  if (!data.place[0] || !date(data.observed, strlen(data.observed), true) || !temperature(data.temperature) ||
      !temperature(data.feelsLike) || !validWeatherCode(data.code))
    return false;
  for (unsigned i = 0; i < 3; ++i) {
    const auto& day = data.days[i];
    if (!date(day.date, strlen(day.date), false) || !temperature(day.low) || !temperature(day.high) ||
        day.low > day.high || !validWeatherCode(day.code) || (i && strcmp(data.days[i - 1].date, day.date) >= 0))
      return false;
  }
  return true;
}
struct Context {
  WeatherData data;
  char key[32]{};
  unsigned depth = 0;
  Scope scope = Scope::None;
  Field array = Field::None;
  unsigned index = 0;
  uint16_t currentMask = 0;
  uint16_t dailyMask = 0;
  uint8_t rootMask = 0;
  bool unitSeen = false;
  bool ok = true;
};
Field field(const char* key) {
  if (!strcmp(key, "time")) return Field::Time;
  if (!strcmp(key, "temperature_2m")) return Field::Temperature;
  if (!strcmp(key, "apparent_temperature")) return Field::Feels;
  if (!strcmp(key, "weather_code")) return Field::Code;
  if (!strcmp(key, "is_day")) return Field::Daylight;
  if (!strcmp(key, "temperature_2m_min")) return Field::Low;
  if (!strcmp(key, "temperature_2m_max")) return Field::High;
  return Field::None;
}
bool currentField(Field f) { return f >= Field::Time && f <= Field::Daylight; }
bool dailyField(Field f) { return f == Field::Time || f == Field::Code || f == Field::Low || f == Field::High; }
void key(void* user, const char* value, size_t length) {
  auto& c = *static_cast<Context*>(user);
  if (length >= sizeof(c.key) || memchr(value, 0, length))
    c.key[0] = 0;
  else {
    memcpy(c.key, value, length);
    c.key[length] = 0;
  }
}
Field scalar(Context& c) {
  if (c.depth == 1 && (!strcmp(c.key, "current") || !strcmp(c.key, "daily") || !strcmp(c.key, "current_units")))
    c.ok = false;
  if (c.depth == 2 && c.scope == Scope::Units && !strcmp(c.key, "temperature_2m")) c.ok = false;
  if (c.array != Field::None && c.depth == 3) {
    if (c.index >= 3) {
      c.ok = false;
      return Field::None;
    }
    return c.array;
  }
  const Field f = field(c.key);
  if (c.scope == Scope::Current && c.depth == 2 && currentField(f)) {
    const uint16_t bit = 1u << static_cast<unsigned>(f);
    if (c.currentMask & bit) c.ok = false;
    c.currentMask |= bit;
    return f;
  }
  if (c.scope == Scope::Daily && c.depth == 2 && dailyField(f)) c.ok = false;
  return Field::None;
}
void string(void* user, const char* value, size_t length) {
  auto& c = *static_cast<Context*>(user);
  if (c.scope == Scope::Units && c.depth == 2 && !strcmp(c.key, "temperature_2m")) {
    const char* expected = c.data.fahrenheit ? "\xc2\xb0"
                                               "F"
                                             : "\xc2\xb0"
                                               "C";
    if (c.unitSeen || length != 3 || memcmp(value, expected, 3)) c.ok = false;
    c.unitSeen = true;
    return;
  }
  const auto f = scalar(c);
  if (f == Field::None) return;
  if (f != Field::Time || !date(value, length, c.array == Field::None)) {
    c.ok = false;
    return;
  }
  char* destination = c.array == Field::None ? c.data.observed : c.data.days[c.index++].date;
  memcpy(destination, value, length);
  destination[length] = 0;
}
void number(void* user, const char* value, size_t length) {
  auto& c = *static_cast<Context*>(user);
  const auto f = scalar(c);
  if (f == Field::None) return;
  float parsed = 0;
  if (!numeric(value, length, parsed)) {
    c.ok = false;
    return;
  }
  const bool daily = c.array != Field::None;
  switch (f) {
    case Field::Temperature:
      c.data.temperature = parsed;
      break;
    case Field::Feels:
      c.data.feelsLike = parsed;
      break;
    case Field::Code:
      if (parsed != static_cast<int>(parsed) || !validWeatherCode(static_cast<int>(parsed)))
        c.ok = false;
      else if (daily)
        c.data.days[c.index].code = static_cast<uint8_t>(parsed);
      else
        c.data.code = static_cast<uint8_t>(parsed);
      break;
    case Field::Daylight:
      if (parsed != 0 && parsed != 1) c.ok = false;
      c.data.daylight = parsed == 1;
      break;
    case Field::Low:
      c.data.days[c.index].low = parsed;
      break;
    case Field::High:
      c.data.days[c.index].high = parsed;
      break;
    default:
      c.ok = false;
      break;
  }
  if (daily) ++c.index;
}
void invalidScalar(void* user) {
  auto& c = *static_cast<Context*>(user);
  if (scalar(c) != Field::None || (c.scope == Scope::Units && c.depth == 2 && !strcmp(c.key, "temperature_2m")))
    c.ok = false;
}
void boolean(void* user, bool) { invalidScalar(user); }
void prefix(void* user, const char*, size_t) { invalidScalar(user); }
void objectStart(void* user) {
  auto& c = *static_cast<Context*>(user);
  if (c.depth == 1) {
    unsigned bit = 0;
    if (!strcmp(c.key, "current")) {
      c.scope = Scope::Current;
      bit = 1;
    } else if (!strcmp(c.key, "daily")) {
      c.scope = Scope::Daily;
      bit = 2;
    } else if (!strcmp(c.key, "current_units")) {
      c.scope = Scope::Units;
      bit = 4;
    } else
      c.scope = Scope::None;
    if (c.rootMask & bit) c.ok = false;
    c.rootMask |= bit;
  } else if (c.depth && (scalar(c) != Field::None || c.array != Field::None))
    c.ok = false;
  ++c.depth;
  c.key[0] = 0;
}
void objectEnd(void* user) {
  auto& c = *static_cast<Context*>(user);
  if (c.depth == 2) c.scope = Scope::None;
  if (c.depth) --c.depth;
}
void arrayStart(void* user) {
  auto& c = *static_cast<Context*>(user);
  const auto f = field(c.key);
  if (c.depth == 2 && c.scope == Scope::Daily && dailyField(f)) {
    const uint16_t bit = 1u << static_cast<unsigned>(f);
    if (c.dailyMask & bit) c.ok = false;
    c.dailyMask |= bit;
    c.array = f;
    c.index = 0;
  } else if (!c.depth || scalar(c) != Field::None || c.array != Field::None)
    c.ok = false;
  ++c.depth;
  c.key[0] = 0;
}
void arrayEnd(void* user) {
  auto& c = *static_cast<Context*>(user);
  if (c.depth == 3 && c.array != Field::None) {
    if (c.index != 3) c.ok = false;
    c.array = Field::None;
  }
  if (c.depth) --c.depth;
}
}  // namespace
bool parseWeather(const char* path, bool fahrenheit, const char* place, WeatherData& result) {
  if (!place || !*place || strlen(place) >= sizeof(result.place)) return false;
  auto c = makeUniqueNoThrow<Context>();
  if (!c) return false;
  c->data.fahrenheit = fahrenheit;
  snprintf(c->data.place, sizeof(c->data.place), "%s", place);
  JsonCallbacks callbacks{c.get(),     key,       string,     number,   boolean, invalidScalar,
                          objectStart, objectEnd, arrayStart, arrayEnd, prefix};
  auto parser = makeUniqueNoThrow<StreamingJsonParser>(callbacks);
  auto buffer = makeUniqueNoThrow<char[]>(1024);
  auto file = Storage.open(path);
  if (!parser || !buffer || !file || file.size() > 32768) return false;
  JsonStructure framing;
  while (file.available()) {
    const int size = file.read(buffer.get(), 1024);
    if (size <= 0 || !framing.feed(buffer.get(), size)) return false;
    parser->feed(buffer.get(), size);
    if (!c->ok || parser->hasError()) return false;
  }
  parser->feed(" ", 1);
  constexpr unsigned currentRequired = (1u << 1) | (1u << 2) | (1u << 3) | (1u << 4) | (1u << 5);
  constexpr unsigned dailyRequired = (1u << 1) | (1u << 4) | (1u << 6) | (1u << 7);
  if (!c->ok || c->depth || c->rootMask != 7 || c->currentMask != currentRequired || c->dailyMask != dailyRequired ||
      !c->unitSeen || parser->hasError() || !framing.finish() || !valid(c->data))
    return false;
  result = c->data;
  return true;
}
bool saveWeather(const WeatherData& data) {
  if (!valid(data)) return false;
  CacheWriter writer;
  auto item = makeUniqueNoThrow<Item>();
  if (!item || !writer.begin(Dataset::Weather)) return false;
  snprintf(item->id, sizeof(item->id), "weather-v1");
  snprintf(item->title, sizeof(item->title), "%s", data.place);
  item->flags = data.fahrenheit ? 1 : 0;
  if (!writer.append(*item)) return false;
  *item = {};
  snprintf(item->id, sizeof(item->id), "current");
  snprintf(item->title, sizeof(item->title), "%.1f", data.temperature);
  snprintf(item->detail, sizeof(item->detail), "%.1f", data.feelsLike);
  snprintf(item->target, sizeof(item->target), "%u %u", data.code, unsigned(data.daylight));
  snprintf(item->when, sizeof(item->when), "%s", data.observed);
  if (!writer.append(*item)) return false;
  for (const auto& day : data.days) {
    *item = {};
    snprintf(item->id, sizeof(item->id), "%s", day.date);
    snprintf(item->title, sizeof(item->title), "%.1f", day.low);
    snprintf(item->detail, sizeof(item->detail), "%.1f", day.high);
    snprintf(item->target, sizeof(item->target), "%u", day.code);
    if (!writer.append(*item)) return false;
  }
  return writer.commit();
}
bool loadWeather(WeatherData& data) {
  CacheReader reader;
  auto item = makeUniqueNoThrow<Item>();
  auto loaded = makeUniqueNoThrow<WeatherData>();
  if (!item || !loaded || !reader.open(Dataset::Weather) || reader.size() != 5 || !reader.read(0, *item) ||
      strcmp(item->id, "weather-v1") || strlen(item->title) >= sizeof(loaded->place) || item->flags > 1)
    return false;
  snprintf(loaded->place, sizeof(loaded->place), "%s", item->title);
  loaded->fahrenheit = item->flags != 0;
  loaded->syncedAt = reader.timestamp();
  unsigned code = 0, daylight = 0;
  int end = 0;
  if (!reader.read(1, *item) || strcmp(item->id, "current") ||
      !numeric(item->title, strlen(item->title), loaded->temperature) ||
      !numeric(item->detail, strlen(item->detail), loaded->feelsLike) ||
      sscanf(item->target, "%u %u%n", &code, &daylight, &end) != 2 || item->target[end] || daylight > 1 || code > 99 ||
      !validWeatherCode(code) || strlen(item->when) >= sizeof(loaded->observed))
    return false;
  loaded->code = static_cast<uint8_t>(code);
  loaded->daylight = daylight != 0;
  snprintf(loaded->observed, sizeof(loaded->observed), "%s", item->when);
  for (unsigned i = 0; i < 3; ++i) {
    auto& day = loaded->days[i];
    if (!reader.read(i + 2, *item) || strlen(item->id) != 10 || !numeric(item->title, strlen(item->title), day.low) ||
        !numeric(item->detail, strlen(item->detail), day.high) || sscanf(item->target, "%u%n", &code, &end) != 1 ||
        item->target[end] || code > 99 || !validWeatherCode(code))
      return false;
    memcpy(day.date, item->id, 11);
    day.code = static_cast<uint8_t>(code);
  }
  if (!valid(*loaded)) return false;
  data = *loaded;
  return true;
}
}  // namespace crossloop::personal

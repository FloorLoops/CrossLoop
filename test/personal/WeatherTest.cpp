#include <cmath>
#include <iostream>
#include <string>

#include "util/PersonalCache.h"
#include "util/WeatherData.h"

int checks = 0, failures = 0;
#define CHECK(x)                             \
  do {                                       \
    ++checks;                                \
    if (!(x)) {                              \
      ++failures;                            \
      std::cerr << __LINE__ << ": " #x "\n"; \
    }                                        \
  } while (0)
using namespace crossloop::personal;
const std::string response =
    R"({"latitude":40.7,"current_units":{"temperature_2m":"\u00b0F"},"current":{"time":"2026-11-08T12:30","temperature_2m":51.2,"apparent_temperature":48.7,"weather_code":2,"is_day":1},"daily":{"time":["2026-11-08","2026-11-09","2026-11-10"],"weather_code":[2,63,71],"temperature_2m_min":[40.1,38,30],"temperature_2m_max":[55,50,42]}})";
bool parse(const std::string& json, WeatherData& data, bool fahrenheit = true) {
  fake::files["/raw"] = std::vector<uint8_t>(json.begin(), json.end());
  return parseWeather("/raw", fahrenheit, "Test location", data);
}
std::string changed(const std::string& from, const std::string& to) {
  auto result = response;
  const auto at = result.find(from);
  CHECK(at != std::string::npos);
  if (at != std::string::npos) result.replace(at, from.size(), to);
  return result;
}
int main() {
  WeatherData data;
  CHECK(parse(response, data));
  CHECK(data.fahrenheit && data.daylight && data.code == 2);
  CHECK(std::abs(data.temperature - 51.2f) < .01f);
  CHECK(data.days[1].code == 63 && data.days[2].low == 30);
  CHECK(saveWeather(data));
  const auto original = fake::files[cachePath(Dataset::Weather)];
  WeatherData loaded;
  CHECK(loadWeather(loaded));
  CHECK(loaded.days[2].high == 42 && !strcmp(loaded.place, "Test location"));
  for (size_t n = 0; n < response.size(); ++n) {
    CHECK(!parse(response.substr(0, n), data));
    CHECK(fake::files[cachePath(Dataset::Weather)] == original);
  }
  for (const auto& json : {changed("51.2", "null"),
                           changed("51.2", "\"51.2\""),
                           changed("51.2", "201"),
                           changed("51.2", "1e99"),
                           changed("51.2", "[]"),
                           changed("51.2", "{}"),
                           changed("\"is_day\":1", "\"is_day\":2"),
                           changed("\"weather_code\":2", "\"weather_code\":4"),
                           changed("\"weather_code\":2", "\"weather_code\":2.5"),
                           changed("[2,63,71]", "[2,63]"),
                           changed("[2,63,71]", "[2,63,71,0]"),
                           changed("[2,63,71]", "[2,{},71]"),
                           changed("[2,63,71]", "[2,[63],71]"),
                           changed("[2,63,71]", "[2,true,71]"),
                           changed("[2,63,71]", "[2,63,]"),
                           changed("40.1,38,30", "60,38,30"),
                           changed("2026-11-09", "2026-11-08"),
                           changed("2026-11-10", "2026-02-30"),
                           changed("T12:30", "T24:30"),
                           changed("T12:30", "T12:60"),
                           changed("\\u00b0F", "\\u00b0C"),
                           changed("\"current\":", "\"wrong\":"),
                           changed("\"daily\":", "\"wrong\":"),
                           changed("\"temperature_2m\":51.2", "\"temperature_2m\":51.2,\"temperature_2m\":50"),
                           changed("\"current_units\":", "\"other_units\":")})
    CHECK(!parse(json, data));
  CHECK(!parse(response + "{}", data));
  CHECK(!parse(changed("\"latitude\":40.7", "\"current\":null,\"latitude\":40.7"), data));
  CHECK(
      !parse(changed("\"temperature_2m\":\"\\u00b0F\"", "\"temperature_2m\":\"\\u00b0F\",\"temperature_2m\":5"), data));
  CHECK(!parse(response, data, false));
  CHECK(parse(changed("\\u00b0F", "\\u00b0C"), data, false));
  CHECK(!data.fahrenheit);
  CHECK(parse(response, data));
  fake::writesLeft = 30;
  CHECK(!saveWeather(data));
  fake::writesLeft = -1;
  CHECK(fake::files[cachePath(Dataset::Weather)] == original);
  fake::renameCalls = 0;
  fake::renameFailure = 2;
  CHECK(!saveWeather(data));
  fake::renameFailure = 0;
  CHECK(loadWeather(loaded));
  CHECK(loaded.days[2].code == 71);
  CHECK(saveWeather(data));
  fake::files[cachePath(Dataset::Weather)][100] ^= 1;
  CHECK(loadWeather(loaded));  // Whole previous generation, never mixed records.
  CHECK(loaded.days[2].high == 42);
  fake::files[cachePath(Dataset::Weather) + ".bak"][100] ^= 1;
  CHECK(!loadWeather(loaded));
  std::cout << checks << " checks, " << failures << " failures\n";
  return failures ? 1 : 0;
}

#pragma once

#include <cstdint>

namespace crossloop::personal {
struct WeatherDay {
  char date[11]{};
  float low = 0;
  float high = 0;
  uint8_t code = 0;
};
struct WeatherData {
  char place[65]{};
  char observed[20]{};
  float temperature = 0;
  float feelsLike = 0;
  uint8_t code = 0;
  bool daylight = true;
  bool fahrenheit = true;
  WeatherDay days[3]{};
  uint32_t syncedAt = 0;
};
bool validWeatherCode(int code);
// Three daily forecasts and current conditions, with strict complete-response
// validation. Failed refreshes leave the previous published cache untouched.
bool parseWeather(const char* path, bool fahrenheit, const char* place, WeatherData& result);
bool saveWeather(const WeatherData& data);
bool loadWeather(WeatherData& data);
}  // namespace crossloop::personal

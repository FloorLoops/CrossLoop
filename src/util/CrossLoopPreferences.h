#pragma once

#include <cstddef>
#include <string>

namespace crossloop::preferences {
enum class Key {
  TickTickToken,
  TickTickProjects,
  FeedUrls,
  CalendarUrl,
  Latitude,
  Longitude,
  LocationName,
  Fahrenheit
};
size_t maxLength(Key key);
// Account data is kept in internal NVS, outside SD browsing/export. It is not
// claimed to resist extraction of the device's raw internal flash.
bool read(Key key, std::string& value);
bool write(Key key, const std::string& value);
bool erase(Key key);
bool validValue(Key key, const std::string& value);
}  // namespace crossloop::preferences

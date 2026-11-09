#pragma once

#include <cstddef>
#include <string>

namespace crossloop::books {
inline constexpr const char* DETAILS_SUFFIXES[] = {".crossloop-meta", ".crossloop-meta.bak", ".crossloop-meta.part"};
struct Details {
  char title[193]{};
  char author[129]{};
  char genre[97]{};
  char series[97]{};
  char seriesNumber[17]{};
};
static_assert(sizeof(Details) == 533);
enum class DetailsResult { Missing, Ok, Error };
bool validDetails(const Details& details);
bool emptyDetails(const Details& details);
DetailsResult loadDetails(const std::string& bookPath, Details& details);
// Empty fields inherit embedded metadata. A valid empty record resets overrides.
// Unknown/corrupt existing records are preserved unless a valid backup exists.
bool saveDetails(const std::string& bookPath, const Details& details);
}  // namespace crossloop::books

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace crossloop {
// Expat retains names for the whole document. Bound distinct names and
// namespace declarations, including prefixes that expand to the same URI.
class XmlNameBudget {
 public:
  bool add(const char* name) {
    if (!name) return true;
    const size_t length = strlen(name);
    if (length > 512) return false;
    for (unsigned i = 0; i < count; ++i)
      if (!strcmp(names + offsets[i], name)) return true;
    if (count == 96 || length + 1 > sizeof(names) - used) return false;
    offsets[count++] = used;
    memcpy(names + used, name, length + 1);
    used += static_cast<uint16_t>(length + 1);
    return true;
  }

 private:
  char names[4096]{};
  uint16_t offsets[96]{};
  uint16_t used = 0;
  uint8_t count = 0;
};
}  // namespace crossloop

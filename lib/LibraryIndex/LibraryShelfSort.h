#pragma once

#include <cstdint>

namespace library {
// Temporary sort records stay on SD. Only 32 records and two merge heads are
// resident, so a 4096-book library has the same sorting workspace as 32 books.
struct ShelfKey {
  char name[97]{};
  uint16_t ordinal = 0;
  double number = 0;
};
using ShelfSource = bool (*)(void*, uint16_t, ShelfKey&);
using ShelfOutput = bool (*)(void*, uint16_t);
bool sortShelf(uint16_t count, ShelfSource source, ShelfOutput output, void* context);
}  // namespace library

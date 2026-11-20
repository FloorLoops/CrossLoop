#include <HalMemory.h>
#include <HalStorage.h>

#include <cassert>
#include <cstdio>
#include <cstring>

#include "util/SavedItems.h"
namespace saved = crossloop::saved;
constexpr char path[] = "/.crosspoint/crossloop-saved.bin";
constexpr char backup[] = "/.crosspoint/crossloop-saved.bin.bak";
constexpr char part[] = "/.crosspoint/crossloop-saved.bin.part";
int main() {
  saved::Table table, loaded;
  fake::reset();
  assert(saved::load(table) && table.count == 0);
  assert(saved::put(table, saved::Kind::Book, "First", "/Books/first.epub"));
  assert(saved::put(table, saved::Kind::Book, "Renamed", "/Books/first.epub") && table.count == 1);
  dream_memory::failAfter = 0;
  assert(!saved::put(table, saved::Kind::Book, "Changed", "/Books/first.epub"));
  assert(!strcmp(table.entries[0].title, "Renamed") && table.count == 1);
  assert(saved::put(table, saved::Kind::Web, "Web", "https://example.com/"));
  assert(!saved::put(table, saved::Kind::Web, "Bad", "file:///private"));
  assert(!saved::put(table, saved::Kind::Book, "Bad", "/Books/../private"));
  assert(!saved::put(table, saved::Kind::Book, "\xc0\x80", "/bad.epub"));
  assert(!saved::put(table, saved::Kind::Book, "\xed\xa0\x80", "/bad.epub"));
  assert(!saved::put(table, saved::Kind::Book, "Line\nbreak", "/bad.epub"));
  std::string longTitle(95, 'a');
  longTitle += "\xc3\xa9";
  assert(saved::put(table, saved::Kind::Book, longTitle, "/unicode.epub"));
  assert(strlen(table.entries[2].title) == 95);
  assert(saved::save(table) && saved::load(loaded) && loaded.count == 3);
  const auto first = fake::files.at(path)->bytes;
  assert(saved::erase(table, 1) && table.count == 2);
  assert(saved::save(table) && fake::files.at(backup)->bytes == first);
  const auto second = fake::files.at(path)->bytes;
  fake::files.at(path)->bytes[20] ^= 1;
  assert(saved::load(loaded) && loaded.count == 3);
  assert(saved::save(table) && fake::files.at(backup)->bytes == first);
  for (int fault = 0; fault < 6; ++fault) {
    fake::files.at(path)->bytes = second;
    if (fault == 0) fake::failWrite = 0;
    if (fault == 1) fake::shortWrite = 7;
    if (fault == 2) fake::failClosePath = part;
    if (fault == 3) fake::failRename = 0;
    if (fault == 4) fake::failRename = 1;
    if (fault == 5) dream_memory::failAfter = 0;
    assert(!saved::save(table));
    assert(saved::load(loaded) && loaded.count == 2);
    assert(dream_memory::allocations == 0);
  }
  fake::add(backup, "");
  fake::files.at(backup)->bytes = first;
  fake::files.at(path)->bytes[0] = 'X';
  fake::files.at(backup)->bytes[0] = 'Y';
  const auto broken = fake::files.at(path)->bytes;
  assert(!saved::load(loaded) && loaded.count == 0);
  assert(!saved::save(table) && fake::files.at(path)->bytes == broken);
  fake::reset();
  assert(saved::toggle(saved::Kind::Book, "Book", "/book.epub") == saved::Result::Saved);
  assert(saved::contains(saved::Kind::Book, "/book.epub"));
  assert(saved::toggle(saved::Kind::Book, "Book", "/book.epub") == saved::Result::Removed);
  dream_memory::failAfter = 0;
  assert(saved::toggle(saved::Kind::Book, "Book", "/book.epub") == saved::Result::NoMemory);
  assert(!saved::contains(saved::Kind::Book, "/book.epub"));
  table.count = 0;
  for (unsigned i = 0; i < saved::CAPACITY; ++i)
    assert(saved::put(table, saved::Kind::Book, "Book", "/book" + std::to_string(i) + ".epub"));
  assert(!saved::put(table, saved::Kind::Book, "Extra", "/extra.epub"));
  assert(saved::save(table) && saved::load(loaded) && loaded.count == saved::CAPACITY);
  assert(saved::toggle(saved::Kind::Book, "Extra", "/extra.epub") == saved::Result::Full);
  fake::add(part);
  fake::files.at(part)->directory = true;
  assert(!saved::save(table) && fake::files.at(part)->directory);
  assert(dream_memory::allocations == 0);
  std::puts("Saved items: persistence, recovery, bounds, UTF-8 and fault checks passed");
}

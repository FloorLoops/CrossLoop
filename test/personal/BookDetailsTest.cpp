#include <HalStorage.h>
#include <LibraryShelfSort.h>

#include <algorithm>
#include <cstring>
#include <iostream>
#include <limits>
#include <vector>

#include "util/BookDetails.h"

static unsigned checks = 0, failures = 0;
#define CHECK(x)                                   \
  do {                                             \
    ++checks;                                      \
    if (!(x)) {                                    \
      ++failures;                                  \
      std::cerr << __LINE__ << ": " << #x << '\n'; \
    }                                              \
  } while (0)
namespace books = crossloop::books;
struct SortContext {
  std::vector<library::ShelfKey> keys;
  std::vector<uint16_t> output;
  int failAt = -1;
};
bool source(void* context, uint16_t index, library::ShelfKey& key) {
  auto& c = *static_cast<SortContext*>(context);
  if (index == c.failAt) return false;
  key = c.keys[index];
  return true;
}
bool output(void* context, uint16_t index) {
  static_cast<SortContext*>(context)->output.push_back(index);
  return true;
}
int main() {
  fake::reset();
  books::Details draft, loaded;
  const std::string path = "/Books/one.epub";
  fake::add(path);
  CHECK(books::loadDetails(path, loaded) == books::DetailsResult::Missing);
  strcpy(draft.title, "My title");
  strcpy(draft.author, "An author");
  strcpy(draft.genre, "Science fiction");
  strcpy(draft.series, "A series");
  strcpy(draft.seriesNumber, "0.5");
  CHECK(books::validDetails(draft) && !books::emptyDetails(draft));
  CHECK(books::saveDetails(path, draft));
  CHECK(books::loadDetails(path, loaded) == books::DetailsResult::Ok && !memcmp(&draft, &loaded, sizeof(draft)));
  const auto original = fake::files[path + books::DETAILS_SUFFIXES[0]]->bytes;
  for (int cut = 0; cut < int(original.size()); ++cut) {
    fake::files[path + books::DETAILS_SUFFIXES[0]]->bytes = original;
    fake::files[path + books::DETAILS_SUFFIXES[0]]->bytes.resize(cut);
    CHECK(books::loadDetails(path, loaded) == books::DetailsResult::Error);
    CHECK(!books::saveDetails(path, draft));
  }
  fake::files[path + books::DETAILS_SUFFIXES[0]]->bytes = original;
  strcpy(draft.title, "A changed title");
  CHECK(books::saveDetails(path, draft));
  CHECK(fake::files[path + books::DETAILS_SUFFIXES[1]]->bytes == original);
  fake::files[path + books::DETAILS_SUFFIXES[0]]->bytes[20] ^= 1;
  CHECK(books::loadDetails(path, loaded) == books::DetailsResult::Ok && !strcmp(loaded.title, "My title"));
  CHECK(books::saveDetails(path, draft));
  CHECK(books::loadDetails(path, loaded) == books::DetailsResult::Ok && !strcmp(loaded.title, "A changed title"));
  fake::files.erase(path + books::DETAILS_SUFFIXES[0]);
  CHECK(books::loadDetails(path, loaded) == books::DetailsResult::Ok);
  CHECK(books::saveDetails(path, draft));
  auto valid = fake::files[path + books::DETAILS_SUFFIXES[0]]->bytes;
  fake::shortWrite = 0;
  CHECK(!books::saveDetails(path, draft));
  CHECK(fake::files[path + books::DETAILS_SUFFIXES[0]]->bytes == valid);
  fake::failRename = 1;
  CHECK(!books::saveDetails(path, draft));
  CHECK(books::loadDetails(path, loaded) == books::DetailsResult::Ok);
  fake::failAlloc = 0;
  CHECK(!books::saveDetails(path, draft));
  CHECK(books::loadDetails(path, loaded) == books::DetailsResult::Ok);
  const auto before = draft;
  for (const auto* number : {"nan", "inf", "1e9", "-1", "1.2.3", "1000001", "."}) {
    strcpy(draft.seriesNumber, number);
    CHECK(!books::validDetails(draft));
  }
  draft = before;
  draft.title[0] = '\n';
  CHECK(!books::validDetails(draft));
  draft = before;
  memset(draft.title, 'a', sizeof(draft.title));
  CHECK(!books::validDetails(draft));
  draft = before;
  strcpy(draft.title, "\xc0\xaf");
  CHECK(!books::validDetails(draft));
  strcpy(draft.title, "\xed\xa0\x80");
  CHECK(!books::validDetails(draft));
  strcpy(draft.title, "Caf\xc3\xa9");
  CHECK(books::validDetails(draft));
  draft = {};
  CHECK(books::emptyDetails(draft) && books::saveDetails(path, draft));
  CHECK(books::loadDetails(path, loaded) == books::DetailsResult::Ok && books::emptyDetails(loaded));
  fake::add(path + books::DETAILS_SUFFIXES[2], "");
  fake::files[path + books::DETAILS_SUFFIXES[2]]->directory = true;
  CHECK(!books::saveDetails(path, draft));
  for (unsigned count : {0u, 1u, 31u, 32u, 33u, 63u, 64u, 65u, 255u, 256u, 4096u}) {
    fake::reset();
    SortContext c;
    for (unsigned i = 0; i < count; ++i) {
      library::ShelfKey key;
      if (i % 9) snprintf(key.name, sizeof(key.name), "A very long shared prefix %03u", (count - i) % 17);
      key.ordinal = static_cast<uint16_t>(i);
      key.number = i % 7 == 0 ? std::numeric_limits<double>::infinity() : (count - i) % 13 * 0.5;
      c.keys.push_back(key);
    }
    auto expected = c.keys;
    std::sort(expected.begin(), expected.end(), [](const auto& a, const auto& b) {
      if (!a.name[0] != !b.name[0]) return a.name[0] != 0;
      if (const int diff = strcmp(a.name, b.name)) return diff < 0;
      return a.number != b.number ? a.number < b.number : a.ordinal < b.ordinal;
    });
    CHECK(library::sortShelf(static_cast<uint16_t>(count), source, output, &c));
    CHECK(c.output.size() == expected.size());
    if (c.output.size() == expected.size())
      for (unsigned i = 0; i < count; ++i) CHECK(c.output[i] == expected[i].ordinal);
    CHECK(!Storage.exists("/.crosspoint/library-shelf.a") && !Storage.exists("/.crosspoint/library-shelf.b"));
    if (count > 32) {
      c.output.clear();
      c.failAt = 32;
      CHECK(!library::sortShelf(static_cast<uint16_t>(count), source, output, &c));
      CHECK(c.output.empty());
      c.failAt = -1;
      fake::failWrite = 1;
      CHECK(!library::sortShelf(static_cast<uint16_t>(count), source, output, &c));
      CHECK(c.output.empty());
      fake::failWrite = -1;
      fake::failRead = 1;
      CHECK(!library::sortShelf(static_cast<uint16_t>(count), source, output, &c));
      CHECK(c.output.empty());
    }
  }
  std::cout << checks << " book-details and shelf-sort checks; " << failures << " failures\n";
  return failures ? 1 : 0;
}

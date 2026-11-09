#include <iostream>

#include "util/WebPageStore.h"

int checks = 0, failures = 0, invalidations = 0;
#define CHECK(x)                             \
  do {                                       \
    ++checks;                                \
    if (!(x)) {                              \
      ++failures;                            \
      std::cerr << __LINE__ << ": " #x "\n"; \
    }                                        \
  } while (0)
void clearBookCache(const std::string&) { ++invalidations; }
void raw(const std::string& value) { fake::files["/raw"] = std::vector<uint8_t>(value.begin(), value.end()); }
void resetFailures() {
  fake::writesLeft = -1;
  fake::failReplace = fake::failClose = false;
  fake::shortReadAt = -1;
  fake::renameFailure = fake::renameCalls = 0;
}
int main() {
  using namespace crossloop::web;
  const std::string url = "https://example.com/a/index.html";
  std::string key;
  raw("<p>First page</p><a href='../next'>Next</a>");
  CHECK(importPage("/raw", url, false, key));
  PageInfo info;
  CHECK(readPageInfo(key, info));
  CHECK(std::string(info.url) == url);
  CHECK(info.links == 1);
  PageLink link;
  CHECK(readPageLink(key, 0, link));
  CHECK(std::string(link.url) == "https://example.com/next");
  CHECK(std::string(link.label) == "Next");
  CHECK(!readPageLink(key, 32, link));
  CHECK(!readPageInfo("../other", info));
  std::string text;
  CHECK(exportPageText(key, text));
  CHECK(invalidations == 1);
  CHECK(std::string(fake::files[text].begin(), fake::files[text].end()) == "First page\nNext [1]");
  const auto saved = fake::files[pagePath(key)];
  const auto savedText = fake::files[text];
  for (int point : {0, 1, 1024, static_cast<int>(BODY_OFFSET - 1), static_cast<int>(BODY_OFFSET + 5)}) {
    raw("<p>Second page with changed content</p>");
    fake::writesLeft = point;
    CHECK(!importPage("/raw", url, false, key));
    CHECK(fake::files[pagePath(key)] == saved);
    resetFailures();
  }
  fake::failClose = true;
  CHECK(!importPage("/raw", url, false, key));
  CHECK(fake::files[pagePath(key)] == saved);
  resetFailures();
  fake::failReplace = true;
  CHECK(!importPage("/raw", url, false, key));
  CHECK(fake::files[pagePath(key)] == saved);
  fake::files[pagePath(key)] = saved;
  resetFailures();
  // The second rename fails after the old primary became a backup.
  fake::renameFailure = 2;
  CHECK(!importPage("/raw", url, false, key));
  CHECK(readPageInfo(key, info));
  CHECK(fake::files[pagePath(key)] == saved);
  resetFailures();
  // Simulated power interruption with only the previous complete .bak left.
  fake::files[pagePath(key) + ".bak"] = saved;
  fake::files.erase(pagePath(key));
  CHECK(readPageInfo(key, info));
  CHECK(readPageLink(key, 0, link));
  CHECK(std::string(link.label) == "Next");
  CHECK(exportPageText(key, text));
  CHECK(fake::files[text] == savedText);
  fake::files[pagePath(key)] = saved;
  fake::files.erase(pagePath(key) + ".bak");
  raw(std::string(MAX_TEXT + 1, 'x'));
  CHECK(!importPage("/raw", url, true, key));
  CHECK(fake::files[pagePath(key)] == saved);
  fake::writesLeft = 5;
  CHECK(!exportPageText(key, text));
  CHECK(fake::files[text] == savedText);
  resetFailures();
  fake::files[pagePath(key)].pop_back();
  CHECK(!readPageInfo(key, info));
  fake::files[pagePath(key)] = saved;
  fake::files[pagePath(key)][12] = 33;
  CHECK(!readPageInfo(key, info));
  fake::files[pagePath(key)] = saved;
  fake::files[pagePath(key)][0] = 'X';
  CHECK(!readPageInfo(key, info));
  std::cout << checks << " web storage checks; " << failures << " failures\n";
  return failures ? 1 : 0;
}

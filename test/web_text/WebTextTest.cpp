#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include "network/WebUrl.h"
#include "util/WebHistory.h"
#include "util/WebTextLayout.h"
#include "util/WebTextStream.h"

int checks = 0, failures = 0;
#define CHECK(x)                             \
  do {                                       \
    ++checks;                                \
    if (!(x)) {                              \
      ++failures;                            \
      std::cerr << __LINE__ << ": " #x "\n"; \
    }                                        \
  } while (0)

struct Capture {
  std::string text;
  std::vector<std::pair<std::string, std::string>> links;
  std::vector<size_t> markers;
  size_t limit = 1000000;
  bool linksOk = true;
  static bool emit(void* ctx, const char* data, size_t n) {
    auto& c = *static_cast<Capture*>(ctx);
    if (c.text.size() + n > c.limit) return false;
    c.text.append(data, n);
    return true;
  }
  static bool link(void* ctx, const char* url, const char* label) {
    auto& c = *static_cast<Capture*>(ctx);
    if (!c.linksOk) return false;
    c.links.emplace_back(url, label);
    c.markers.push_back(c.text.size());
    return true;
  }
};
Capture parse(const std::string& html, size_t chunk, bool plain = false) {
  Capture result;
  WebTextStream parser({&result, Capture::emit, Capture::link}, plain);
  for (size_t i = 0; i < html.size(); i += chunk) CHECK(parser.feed(html.data() + i, std::min(chunk, html.size() - i)));
  CHECK(parser.finish());
  return result;
}
int main() {
  using namespace crossloop::web;
  CHECK(mediaTypeIs("text/html", "text/html"));
  CHECK(mediaTypeIs(" TeXT/HTML ; charset=UTF-8", "text/html"));
  CHECK(mediaTypeIs("text/plain\t", "text/plain"));
  CHECK(mediaTypeIs("application/xhtml+xml;charset=utf-8", "application/xhtml+xml"));
  CHECK(!mediaTypeIs("text/htmljunk", "text/html"));
  CHECK(!mediaTypeIs("text/plain+json", "text/plain"));
  CHECK(!mediaTypeIs("text/htm", "text/html"));
  CHECK(!mediaTypeIs("", "text/html"));
  CHECK(!mediaTypeIs("text/html\r\n", "text/html"));
  CHECK(searchUrl("  cats & dogs  ", SearchEngine::Google) ==
        "https://www.google.com/search?gbv=1&q=cats%20%26%20dogs");
  CHECK(searchUrl("caf\xc3\xa9", SearchEngine::Wikipedia).ends_with("caf%C3%A9"));
  CHECK(searchUrl(" \t\n", SearchEngine::Google).empty());
  CHECK(searchUrl(std::string(257, 'x'), SearchEngine::Google).empty());
  CHECK(validUrl(searchUrl(std::string(256, '&'), SearchEngine::Wikipedia)));
  CHECK(searchUrl("a#b?c=1%", SearchEngine::Google).ends_with("a%23b%3Fc%3D1%25"));
  History history;
  const std::string a(32, 'a'), b(32, 'b'), c(32, 'c');
  CHECK(!history.canBack() && !history.canForward());
  history.visit(a);
  history.visit(b);
  history.visit(c);
  CHECK(std::string(history.back()) == b);
  history.moveBack();
  CHECK(std::string(history.current()) == b && std::string(history.forward()) == c);
  history.visit(b);  // Opening the same page preserves forward history.
  CHECK(history.canForward());
  history.visit(a);  // A new branch discards forward history.
  CHECK(!history.canForward());
  history.visit(c, true);
  CHECK(std::string(history.current()) == c && std::string(history.back()) == b);
  history.visit("bad");
  CHECK(std::string(history.current()) == c);
  for (unsigned i = 0; i < 100; ++i) {
    char key[33];
    snprintf(key, sizeof(key), "%032x", i);
    history.visit(key);
  }
  unsigned backSteps = 0;
  while (history.canBack()) {
    history.moveBack();
    ++backSteps;
  }
  CHECK(backSteps == History::CAPACITY - 1);
  CHECK(std::string(history.current()) == "00000000000000000000000000000044");
  unsigned forwardSteps = 0;
  while (history.canForward()) {
    history.moveForward();
    ++forwardSteps;
  }
  CHECK(forwardSteps == backSteps);
  History erased;
  erased.visit(a);
  erased.visit(b);
  erased.visit(a);
  erased.visit(c);
  erased.erase(a);
  CHECK(std::string(erased.current()) == c && std::string(erased.back()) == b);
  erased.erase(c);
  CHECK(std::string(erased.current()) == b && !erased.canForward());
  erased.erase(b);
  CHECK(!erased.canBack() && !erased.canForward() && std::string(erased.current()).empty());
  CHECK(completePrefix("a\xf0\x9f\x98") == 1);
  CHECK(completePrefix("a\xf0\x9f\x98\x80") == 5);
  CHECK(completePrefix("\xe2\x82") == 0);
  CHECK(completePrefix("abc") == 3);
  char unfinished[] = "hello \xe2\x82";
  CHECK(prepareChunk(unfinished, sizeof(unfinished) - 1, false) == 6);
  CHECK(static_cast<unsigned char>(unfinished[6]) == 0xe2);
  CHECK(prepareChunk(unfinished, sizeof(unfinished) - 1, true) == 8);
  CHECK(std::string(unfinished) == "hello ??");
  char onlyPartial[] = "\xf0\x9f";
  CHECK(prepareChunk(onlyPartial, sizeof(onlyPartial) - 1, true) == 2);
  CHECK(std::string(onlyPartial) == "??");
  const auto asciiWidth = [](size_t n) { return static_cast<int>(n); };
  auto line = wrapLine("hello world", 7, asciiWidth);
  CHECK(line.length == 5 && line.consumed == 6);
  line = wrapLine("abcdef", 3, asciiWidth);
  CHECK(line.length == 3 && line.consumed == 3);
  line = wrapLine("\nabc", 3, asciiWidth);
  CHECK(line.length == 0 && line.consumed == 1);
  line = wrapLine("word\nnext", 4, asciiWidth);
  CHECK(line.length == 4 && line.consumed == 5);
  line = wrapLine("wide", 0, asciiWidth);
  CHECK(line.length == 1 && line.consumed == 1);
  line = wrapLine("\xc3\xa9\xc3\xa9", 3, asciiWidth);
  CHECK(line.length == 2 && line.consumed == 2);
  unsigned probes = 0;
  line = wrapLine(std::string(8192, 'a'), 80, [&probes](size_t n) {
    ++probes;
    return static_cast<int>(n);
  });
  CHECK(line.length == 80 && probes <= 14);
  const std::string base = "https://example.com/a/b/page.html?old=1";
  CHECK(validUrl(base));
  CHECK(!validUrl("file:///etc/passwd"));
  CHECK(!validUrl("https://user:secret@example.com"));
  CHECK(!validUrl("http://example.com", true));
  CHECK(!validUrl("https://example.com\r\nInjected: yes"));
  CHECK(!validUrl("https://example.com\\other"));
  CHECK(!validUrl("https://"));
  CHECK(!validUrl("https://" + std::string(1024, 'a')));
  const std::pair<const char*, const char*> references[] = {{"child", "https://example.com/a/b/child"},
                                                            {"../next", "https://example.com/a/next"},
                                                            {"../../../../top", "https://example.com/top"},
                                                            {"/root", "https://example.com/root"},
                                                            {"?new=2", "https://example.com/a/b/page.html?new=2"},
                                                            {"#same", "https://example.com/a/b/page.html?old=1"},
                                                            {"//other.test/path", "https://other.test/path"},
                                                            {"https://other.test/a/../b", "https://other.test/b"},
                                                            {"./", "https://example.com/a/b/"},
                                                            {"..", "https://example.com/a/"},
                                                            {"javascript:alert(1)", ""},
                                                            {"mailto:example@example.com", ""},
                                                            {"data:text/html,test", ""},
                                                            {"https://user@evil.test/", ""},
                                                            {"https://good.test/\n", ""}};
  for (const auto& pair : references) CHECK(resolveUrl(base, pair.first) == pair.second);
  CHECK(resolveUrl("https://example.com", "child") == "https://example.com/child");
  const std::string html =
      "<HTML><style>.secret{color:red}</style><!-- hidden -->"
      "<script>secret()</script><p>Hello &amp; welcome &#x1f600;.</p>"
      "<a class='x' href='../next?a=1&amp;b=2'>Next <b>page</b></a>"
      "<p>literal &unknown; &#169; &#xD800; end &</p></HTML>";
  const auto expected = parse(html, html.size());
  CHECK(expected.text.find("secret") == std::string::npos);
  CHECK(expected.text.find("hidden") == std::string::npos);
  CHECK(expected.text.find("Hello & welcome \xf0\x9f\x98\x80.") != std::string::npos);
  CHECK(expected.text.find("literal &unknown; \xc2\xa9; ") == std::string::npos);
  CHECK(expected.text.find("\xc2\xa9 &#xD800; end &") != std::string::npos);
  CHECK(expected.links.size() == 1);
  CHECK(expected.links[0].first == "../next?a=1&b=2");
  CHECK(expected.links[0].second == "Next page");
  CHECK(expected.text.find("[1]") != std::string::npos);
  CHECK(expected.text.substr(expected.markers[0], 3) == "[1]");
  for (size_t size = 1; size <= 63; ++size) {
    const auto chunked = parse(html, size);
    CHECK(chunked.text == expected.text);
    CHECK(chunked.links == expected.links);
    CHECK(chunked.markers == expected.markers);
  }
  CHECK(parse("abc&", 1).text == "abc&");
  CHECK(parse("<tag title='a > b'>visible</tag>", 2).text == "visible");
  CHECK(parse("<a href=\"/x\">x</a>", 1).links[0].first == "/x");
  CHECK(parse("<p data-x='" + std::string(5000, 'x') + "'>kept</p>", 71).text == "kept\n");
  CHECK(parse("<script>" + std::string(100000, 'x') + "</script>fine", 257).text == "fine");
  CHECK(parse("<b>literal</b>&amp;", 1, true).text == "<b>literal</b>&amp;");
  std::string many;
  for (int i = 0; i < 100; ++i) many += "<a href='/a'>item</a>";
  CHECK(parse(many, 17).links.size() == 32);
  for (const char* input :
       {"[1] literal <a href='/x'>go</a>", "<a href='/x'>ends with space </a>", "<a href='/x'><p>x</p></a>"}) {
    const auto result = parse(input, 1);
    CHECK(result.markers.size() == 1 && result.text.substr(result.markers[0], 3) == "[1]");
  }
  Capture limited;
  limited.limit = 100;
  WebTextStream limitParser({&limited, Capture::emit, Capture::link});
  CHECK(!limitParser.feed(std::string(2000, 'a').data(), 2000));
  Capture rejected;
  rejected.linksOk = false;
  WebTextStream rejectParser({&rejected, Capture::emit, Capture::link});
  CHECK(!rejectParser.feed("<a href='/x'>x</a>", 18));
  std::cout << checks << " web parser/URL checks; " << failures << " failures\n";
  return failures ? 1 : 0;
}

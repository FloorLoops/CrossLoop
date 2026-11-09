#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include "network/WebUrl.h"
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
  for (size_t size = 1; size <= 63; ++size) {
    const auto chunked = parse(html, size);
    CHECK(chunked.text == expected.text);
    CHECK(chunked.links == expected.links);
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

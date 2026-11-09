#include <iostream>

#include "util/FeedParser.h"
#include "util/XmlTokenBudget.h"

int checks = 0, failures = 0;
#define CHECK(x)                             \
  do {                                       \
    ++checks;                                \
    if (!(x)) {                              \
      ++failures;                            \
      std::cerr << __LINE__ << ": " #x "\n"; \
    }                                        \
  } while (0)
using namespace crossloop::personal;
bool parse(const std::string& xml) {
  fake::files["/raw"] = std::vector<uint8_t>(xml.begin(), xml.end());
  CacheWriter writer;
  return writer.begin(Dataset::Feeds) && parseFeed("/raw", "https://example.com/feeds/index.xml", writer) &&
         writer.commit();
}
int main() {
  const std::string rss =
      R"(<?xml version="1.0"?><rss version="2.0"><channel><title>Reading</title><item><title>Books &amp; more</title><link>../article</link><description><![CDATA[A short description]]></description><pubDate>Sun, 08 Nov 2026 10:00:00 GMT</pubDate></item></channel></rss>)";
  CHECK(parse(rss));
  CacheReader reader;
  Item item;
  CHECK(reader.open(Dataset::Feeds) && reader.size() == 1 && reader.read(0, item));
  CHECK(std::string(item.title) == "Books & more");
  CHECK(std::string(item.target) == "https://example.com/article");
  CHECK(std::string(item.detail) == "A short description");
  reader.close();
  const auto prior = fake::files[cachePath(Dataset::Feeds)];
  for (size_t i = 0; i < rss.size(); ++i) {
    CHECK(!parse(rss.substr(0, i)));
    CHECK(fake::files[cachePath(Dataset::Feeds)] == prior);
  }
  CHECK(!parse("<rss/>"));
  CHECK(!parse("<html><body>Not a feed</body></html>"));
  CHECK(!parse(
      R"(<!DOCTYPE rss [<!ENTITY ext SYSTEM "file:///secret">]><rss><channel><item><title>&ext;</title></item></channel></rss>)"));
  CHECK(parse(
      R"(<feed xmlns="http://www.w3.org/2005/Atom" xml:base="https://example.com/base/"><entry><title>Atom</title><link rel="self" href="/self"/><link rel="alternate" type="text/html" href="item"/><summary>Saved summary</summary></entry></feed>)"));
  CHECK(reader.open(Dataset::Feeds) && reader.size() == 1 && reader.read(0, item));
  CHECK(std::string(item.target) == "https://example.com/base/item");
  reader.close();
  CHECK(parse(
      R"(<feed xmlns="http://www.w3.org/2005/Atom"><entry xml:base="../news/"><title>Article</title><link href="story"/></entry></feed>)"));
  CHECK(reader.open(Dataset::Feeds) && reader.read(0, item));
  CHECK(std::string(item.target) == "https://example.com/news/story");
  reader.close();
  CHECK(!parse("<rss><channel><item><title>" + std::string(32, '<') + "</title></item></channel></rss>"));
  CHECK(parse(
      "<rss><channel><item><title>Skip unsafe link</title><link>javascript:alert(1)</link></item></channel></rss>"));
  CHECK(reader.open(Dataset::Feeds) && reader.size() == 0);
  reader.close();
  CHECK(!parse("<rss><channel><item><title>A</title><link>https://example.com/" + std::string(1025, 'a') +
               "</link></item></channel></rss>"));
  for (const std::string token : {"<item a='&gt;'>", "<!-- a > b -->", "<![CDATA[a > b]]>", "<?x a > b?>"}) {
    for (size_t i = 0; i <= token.size(); ++i) {
      crossloop::XmlTokenBudget budget;
      CHECK(budget.feed(token.data(), i) && budget.feed(token.data() + i, token.size() - i));
    }
  }
  for (const std::string start : {"<item a='", "<!--", "<![CDATA[", "<?x "}) {
    crossloop::XmlTokenBudget budget;
    CHECK(budget.feed(start.data(), start.size()));
    std::string oversized;
    for (int i = 0; i < 3000; ++i) oversized += ">x";
    CHECK(!budget.feed(oversized.data(), oversized.size()));
  }
  CHECK(!parse(std::string("<rss>\0<channel/></rss>", 22)));
  std::string names = "<rss><channel>";
  for (unsigned i = 0; i < 100; ++i) names += "<name" + std::to_string(i) + "/>";
  CHECK(!parse(names + "</channel></rss>"));
  std::string prefixes = "<rss><channel>";
  for (unsigned i = 0; i < 100; ++i) {
    const auto prefix = "p" + std::to_string(i);
    prefixes += "<" + prefix + ":x xmlns:" + prefix + "='urn:same'/>";
  }
  CHECK(!parse(prefixes + "</channel></rss>"));
  std::string repeated = "<rss><channel>";
  for (unsigned i = 0; i < 200; ++i) repeated += "<same a='value'/>";
  CHECK(parse(repeated + "</channel></rss>"));
  std::string attributes = "<rss><channel><x";
  for (unsigned i = 0; i < 33; ++i) attributes += " a" + std::to_string(i) + "='x'";
  CHECK(!parse(attributes + "/></channel></rss>"));
  CHECK(parse("<rss><channel/></rss>"));
  CHECK(reader.open(Dataset::Feeds) && reader.size() == 0);
  std::cout << checks << " feed checks; " << failures << " failures\n";
  return failures ? 1 : 0;
}

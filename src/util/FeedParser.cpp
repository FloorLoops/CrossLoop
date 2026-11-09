#include "FeedParser.h"

#include <Arduino.h>
#include <Memory.h>
#include <esp_random.h>
#include <expat.h>

#include <algorithm>
#include <cstring>

#include "XmlNameBudget.h"
#include "XmlTokenBudget.h"
#include "network/WebUrl.h"

namespace crossloop::personal {
namespace {
constexpr char ATOM[] = "http://www.w3.org/2005/Atom|";
enum class Capture : uint8_t { None, Title, Link, Date, Summary };
struct Context {
  Context(CacheWriter& writer, const std::string& url) : writer(writer), base(url) {}
  CacheWriter& writer;
  std::string base;
  std::string entryBase;
  XML_Parser parser = nullptr;
  Item item;
  XmlNameBudget names;
  unsigned nodes = 0;
  unsigned depth = 0;
  unsigned itemDepth = 0;
  unsigned captureDepth = 0;
  unsigned items = 0;
  Capture capture = Capture::None;
  bool atom = false;
  bool root = false;
  bool channel = false;
  bool foundChannel = false;
  bool ok = true;
  void fail() {
    ok = false;
    XML_StopParser(parser, XML_FALSE);
  }
};
bool isElement(const Context& self, const char* name, const char* local) {
  if (self.atom) return !strncmp(name, ATOM, sizeof(ATOM) - 1) && !strcmp(name + sizeof(ATOM) - 1, local);
  return !strcmp(name, local);
}
const char* attribute(const XML_Char** attributes, const char* key) {
  for (unsigned i = 0; attributes[i]; i += 2)
    if (!strcmp(attributes[i], key)) return attributes[i + 1];
  return nullptr;
}
void start(void* user, const XML_Char* name, const XML_Char** attributes) {
  auto& self = *static_cast<Context*>(user);
  if (++self.depth > 32 || ++self.nodes > 8192 || !self.names.add(name)) {
    self.fail();
    return;
  }
  for (unsigned i = 0; attributes[i]; i += 2) {
    if (i >= 64 || !self.names.add(attributes[i])) {
      self.fail();
      return;
    }
  }
  if (self.depth == 1) {
    self.atom = !strcmp(name, "http://www.w3.org/2005/Atom|feed");
    self.root = self.atom || !strcmp(name, "rss");
    if (!self.root) {
      self.fail();
      return;
    }
  }
  if (!self.atom && self.depth == 2 && !strcmp(name, "channel")) {
    if (self.foundChannel) {
      self.fail();
      return;
    }
    self.channel = self.foundChannel = true;
  }
  const bool entry = self.atom ? self.depth == 2 && isElement(self, name, "entry")
                               : self.channel && self.depth == 3 && !strcmp(name, "item");
  if (entry) {
    if (++self.items > 256) {
      self.fail();
      return;
    }
    self.item = {};
    self.itemDepth = self.depth;
    self.entryBase = self.base;
  }
  if (const auto* base = attribute(attributes, "http://www.w3.org/XML/1998/namespace|base")) {
    const auto resolved = web::resolveUrl(self.itemDepth ? self.entryBase : self.base, base);
    if (resolved.empty() || (self.depth != 1 && !entry)) {
      self.fail();
      return;
    }
    if (entry)
      self.entryBase = resolved;
    else
      self.base = resolved;
  }
  if (!self.itemDepth || self.depth != self.itemDepth + 1) return;
  self.capture = Capture::None;
  self.captureDepth = self.depth;
  if (isElement(self, name, "title") && !self.item.title[0])
    self.capture = Capture::Title;
  else if (isElement(self, name, "link") && !self.item.target[0]) {
    if (self.atom) {
      const auto* rel = attribute(attributes, "rel");
      const auto* type = attribute(attributes, "type");
      const auto* href = attribute(attributes, "href");
      if (href && (!rel || !strcmp(rel, "alternate")) &&
          (!type || !strcmp(type, "text/html") || !strcmp(type, "application/xhtml+xml"))) {
        if (strlen(href) >= sizeof(self.item.target)) {
          self.fail();
          return;
        }
        memcpy(self.item.target, href, strlen(href) + 1);
      }
    } else
      self.capture = Capture::Link;
  } else if (isElement(self, name, self.atom ? "published" : "pubDate") && !self.item.when[0])
    self.capture = Capture::Date;
  else if (isElement(self, name, self.atom ? "summary" : "description") && !self.item.detail[0])
    self.capture = Capture::Summary;
}
void clean(char* text) {
  size_t begin = 0;
  while (text[begin] && static_cast<unsigned char>(text[begin]) <= 32) ++begin;
  size_t length = strlen(text + begin);
  while (length && static_cast<unsigned char>(text[begin + length - 1]) <= 32) --length;
  memmove(text, text + begin, length);
  text[length] = 0;
}
void end(void* user, const XML_Char* name) {
  auto& self = *static_cast<Context*>(user);
  if (self.captureDepth == self.depth) {
    self.capture = Capture::None;
    self.captureDepth = 0;
  }
  if (self.itemDepth && self.depth == self.itemDepth) {
    clean(self.item.title);
    clean(self.item.target);
    clean(self.item.when);
    clean(self.item.detail);
    const auto url = web::resolveUrl(self.entryBase, self.item.target);
    if (self.item.target[0] && !url.empty()) {
      snprintf(self.item.target, sizeof(self.item.target), "%s", url.c_str());
      if (!self.item.title[0]) snprintf(self.item.title, sizeof(self.item.title), "%s", url.c_str());
      if (!self.writer.append(self.item)) {
        self.fail();
        return;
      }
    }
    self.itemDepth = 0;
  }
  if (!self.atom && self.depth == 2 && !strcmp(name, "channel")) self.channel = false;
  --self.depth;
}
void text(void* user, const XML_Char* text, int size) {
  auto& self = *static_cast<Context*>(user);
  if (size <= 0 || !self.itemDepth || self.capture == Capture::None) return;
  char* target = nullptr;
  size_t capacity = 0;
  switch (self.capture) {
    case Capture::Title:
      target = self.item.title;
      capacity = sizeof(self.item.title);
      break;
    case Capture::Link:
      target = self.item.target;
      capacity = sizeof(self.item.target);
      break;
    case Capture::Date:
      target = self.item.when;
      capacity = sizeof(self.item.when);
      break;
    case Capture::Summary:
      target = self.item.detail;
      capacity = sizeof(self.item.detail);
      break;
    default:
      return;
  }
  const size_t used = strlen(target);
  if (self.capture == Capture::Link && static_cast<size_t>(size) >= capacity - used) {
    self.fail();
    return;
  }
  size_t copy = std::min(static_cast<size_t>(size), capacity - used - 1);
  if (copy < static_cast<size_t>(size))
    while (copy && (static_cast<unsigned char>(text[copy]) & 0xc0) == 0x80) --copy;
  memcpy(target + used, text, copy);
  target[used + copy] = 0;
}
}  // namespace
bool parseFeed(const char* path, const std::string& requestUrl, CacheWriter& writer) {
  if (!web::validUrl(requestUrl)) return false;
  auto context = makeUniqueNoThrow<Context>(writer, requestUrl);
  auto buffer = makeUniqueNoThrow<char[]>(1024);
  if (!context || !buffer) return false;
  context->parser = XML_ParserCreateNS(nullptr, '|');
  if (!context->parser) return false;
  // The SDK's local-book default uses a weak hash salt. Feeds are untrusted
  // network input, so seed this parser explicitly from the platform RNG.
  XML_SetHashSalt(context->parser, static_cast<unsigned long>(esp_random()) | 1UL);
  ScopedCleanup cleanup{[&] { XML_ParserFree(context->parser); }};
  XML_SetUserData(context->parser, context.get());
  XML_SetElementHandler(context->parser, start, end);
  XML_SetStartNamespaceDeclHandler(context->parser, [](void* user, const XML_Char* prefix, const XML_Char* uri) {
    auto& context = *static_cast<Context*>(user);
    if (!context.names.add(prefix) || !context.names.add(uri)) context.fail();
  });
  XML_SetCharacterDataHandler(context->parser, text);
  XML_SetStartDoctypeDeclHandler(context->parser, [](void* user, const XML_Char*, const XML_Char*, const XML_Char*,
                                                     int) { static_cast<Context*>(user)->fail(); });
  XML_SetParamEntityParsing(context->parser, XML_PARAM_ENTITY_PARSING_NEVER);
  auto file = Storage.open(path);
  if (!file || file.size() > 512 * 1024) return false;
  // Expat may retain an incomplete markup token. Cap its length before each
  // feed, so a hostile giant attribute/comment cannot consume the C3 heap.
  XmlTokenBudget tokenBudget;
  while (file.available()) {
    const int count = file.read(buffer.get(), 1024);
    if (count <= 0 || memchr(buffer.get(), 0, count)) return false;
    if (!tokenBudget.feed(buffer.get(), count)) return false;
    if (XML_Parse(context->parser, buffer.get(), count, XML_FALSE) != XML_STATUS_OK || !context->ok) return false;
    delay(1);
  }
  return XML_Parse(context->parser, "", 0, XML_TRUE) == XML_STATUS_OK && context->root && !context->depth &&
         (context->atom || context->foundChannel) && context->ok;
}
}  // namespace crossloop::personal

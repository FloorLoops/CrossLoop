#pragma once

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string_view>

// Streaming reading view, with fixed storage regardless of the source size.
// Scripts, styles, comments and embedded objects are never rendered or run.
class WebTextStream final {
 public:
  struct Sink {
    void* context;
    bool (*text)(void*, const char*, size_t);
    bool (*link)(void*, const char*, const char*);
  };
  explicit WebTextStream(Sink sink, bool plain = false) : sink(sink), plain(plain) {}
  bool feed(const char* data, size_t length) {
    for (size_t i = 0; i < length; ++i) {
      if (!consume(data[i])) return false;
    }
    return true;
  }
  bool finish() {
    if (inEntity && !emitEntity(false)) return false;
    return flush();
  }
  size_t links() const { return linkCount; }

 private:
  bool flush() {
    if (!outputLength) return true;
    const bool ok = sink.text(sink.context, output, outputLength);
    outputLength = 0;
    return ok;
  }
  bool emit(char c) {
    if (c == '\r') c = '\n';
    if (c == '\t' || c == '\f') c = ' ';
    if (c == ' ' && (last == ' ' || last == '\n')) return true;
    if (c == '\n' && last == '\n') return true;
    if (static_cast<unsigned char>(c) < 32 && c != '\n') return true;
    if (outputLength == sizeof(output) && !flush()) return false;
    output[outputLength++] = c;
    last = c;
    if (href[0] && anchorLength + 1 < sizeof(anchor)) {
      anchor[anchorLength++] = c == '\n' ? ' ' : c;
      anchor[anchorLength] = 0;
    }
    return true;
  }
  bool emitEntity(bool terminated) {
    entity[entityLength] = 0;
    const std::string_view name(entity, entityLength);
    uint32_t point = 0;
    if (terminated) {
      if (name == "amp")
        point = '&';
      else if (name == "lt")
        point = '<';
      else if (name == "gt")
        point = '>';
      else if (name == "quot")
        point = '"';
      else if (name == "apos" || name == "#39")
        point = '\'';
      else if (name == "nbsp")
        point = ' ';
      else if (name == "ndash")
        point = 0x2013;
      else if (name == "mdash")
        point = 0x2014;
      else if (name == "hellip")
        point = 0x2026;
      else if (!name.empty() && name[0] == '#') {
        const bool hex = name.size() > 1 && (name[1] == 'x' || name[1] == 'X');
        const uint32_t radix = hex ? 16 : 10;
        for (size_t i = hex ? 2 : 1; i < name.size(); ++i) {
          const unsigned char c = static_cast<unsigned char>(name[i]);
          const uint32_t digit = c >= '0' && c <= '9'   ? c - '0'
                                 : c >= 'a' && c <= 'f' ? c - 'a' + 10
                                 : c >= 'A' && c <= 'F' ? c - 'A' + 10
                                                        : 99;
          if (digit >= radix || point > (0x10ffff - digit) / radix) {
            point = 0;
            break;
          }
          point = point * radix + digit;
        }
      }
    }
    if (point >= 0xd800 && point <= 0xdfff) point = 0;
    if (!point) {
      if (!emit('&')) return false;
      for (size_t i = 0; i < entityLength; ++i)
        if (!emit(entity[i])) return false;
      if (terminated && !emit(';')) return false;
    } else if (point < 128) {
      if (!emit(static_cast<char>(point))) return false;
    } else {
      if (point >= 0x10000 && !emit(static_cast<char>(0xf0 | (point >> 18)))) return false;
      if (point >= 0x800 && !emit(static_cast<char>((point >= 0x10000 ? 0x80 : 0xe0) | ((point >> 12) & 0x3f))))
        return false;
      if (!emit(static_cast<char>((point >= 0x800 ? 0x80 : 0xc0) | ((point >> 6) & 0x3f)))) return false;
      if (!emit(static_cast<char>(0x80 | (point & 0x3f)))) return false;
    }
    entityLength = 0;
    inEntity = false;
    return true;
  }
  static bool isSpace(char c) { return std::isspace(static_cast<unsigned char>(c)) != 0; }
  bool closeTag() {
    if (overflow) return true;
    tag[tagLength] = 0;
    size_t pos = 0;
    while (pos < tagLength && isSpace(tag[pos])) ++pos;
    const bool closing = tag[pos] == '/';
    if (closing) ++pos;
    const size_t start = pos;
    while (pos < tagLength && (std::isalnum(static_cast<unsigned char>(tag[pos])) || tag[pos] == '-')) {
      tag[pos] = static_cast<char>(std::tolower(static_cast<unsigned char>(tag[pos])));
      ++pos;
    }
    const std::string_view name(tag + start, pos - start);
    if (hidden[0]) {
      if (closing && name == hidden) hidden[0] = 0;
      return true;
    }
    if (!closing &&
        (name == "script" || name == "style" || name == "noscript" || name == "iframe" || name == "object")) {
      memcpy(hidden, name.data(), name.size());
      hidden[name.size()] = 0;
      return true;
    }
    if (name == "a" && closing) {
      if (href[0] && linkCount < 32) {
        if (!sink.link(sink.context, href, anchor[0] ? anchor : href)) return false;
        ++linkCount;
        href[0] = 0;
        char marker[12];
        const int length = snprintf(marker, sizeof(marker), " [%u]", static_cast<unsigned>(linkCount));
        for (int i = 0; i < length; ++i)
          if (!emit(marker[i])) return false;
      }
      href[0] = 0;
    } else if (name == "a" && linkCount < 32) {
      href[0] = anchor[0] = 0;
      anchorLength = 0;
      while (pos < tagLength) {
        while (pos < tagLength && isSpace(tag[pos])) ++pos;
        const size_t keyStart = pos;
        while (pos < tagLength && !isSpace(tag[pos]) && tag[pos] != '=') {
          tag[pos] = static_cast<char>(std::tolower(static_cast<unsigned char>(tag[pos])));
          ++pos;
        }
        const std::string_view key(tag + keyStart, pos - keyStart);
        while (pos < tagLength && isSpace(tag[pos])) ++pos;
        if (pos == tagLength) break;
        if (tag[pos] != '=') continue;
        ++pos;
        while (pos < tagLength && isSpace(tag[pos])) ++pos;
        const char delimiter = tag[pos] == '"' || tag[pos] == '\'' ? tag[pos++] : 0;
        const size_t valueStart = pos;
        while (pos < tagLength && (delimiter ? tag[pos] != delimiter : !isSpace(tag[pos]))) ++pos;
        const size_t length = pos - valueStart;
        if (key == "href" && length && length < sizeof(href)) {
          // The common ampersand entity belongs to URL query separators.
          size_t out = 0;
          for (size_t i = valueStart; i < pos; ++i) {
            if (i + 5 <= pos && std::string_view(tag + i, 5) == "&amp;") {
              href[out++] = '&';
              i += 4;
            } else
              href[out++] = tag[i];
          }
          href[out] = 0;
        }
        if (delimiter && pos < tagLength) ++pos;
      }
    }
    if (name == "p" || name == "div" || name == "br" || name == "li" || name == "tr" || name == "section" ||
        name == "article" || name == "header" || name == "footer" || name == "h1" || name == "h2" || name == "h3" ||
        name == "h4" || name == "pre")
      return emit('\n');
    return true;
  }
  bool consume(char c) {
    if (plain) return emit(c);
    if (comment) {
      if (c == '>' && commentDashes >= 2) {
        comment = false;
        commentDashes = 0;
      } else
        commentDashes = c == '-' ? commentDashes + (commentDashes < 2) : 0;
      return true;
    }
    if (inTag) {
      if (!quote && c == '>') {
        inTag = false;
        return closeTag();
      }
      if (quote && c == quote)
        quote = 0;
      else if (!quote && (c == '"' || c == '\''))
        quote = c;
      if (tagLength + 1 < sizeof(tag))
        tag[tagLength++] = c;
      else
        overflow = true;
      if (tagLength == 3 && std::string_view(tag, 3) == "!--") {
        comment = true;
        inTag = false;
      }
      return true;
    }
    if (c == '<') {
      if (inEntity && !emitEntity(false)) return false;
      inTag = true;
      tagLength = 0;
      quote = 0;
      overflow = false;
      return true;
    }
    if (hidden[0]) return true;
    if (inEntity) {
      if (c == ';') return emitEntity(true);
      if (entityLength + 1 < sizeof(entity) && std::isalnum(static_cast<unsigned char>(c))) {
        entity[entityLength++] = c;
        return true;
      }
      if (c == '#' && entityLength == 0) {
        entity[entityLength++] = c;
        return true;
      }
      if (!emitEntity(false)) return false;
    }
    if (c == '&') {
      inEntity = true;
      entityLength = 0;
      return true;
    }
    return emit(c);
  }

  Sink sink;
  bool plain = false;
  bool inTag = false;
  bool overflow = false;
  bool inEntity = false;
  bool comment = false;
  unsigned commentDashes = 0;
  char quote = 0;
  char last = '\n';
  char hidden[12]{};
  char tag[1536]{};
  char href[1025]{};
  char anchor[128]{};
  char entity[24]{};
  char output[512]{};
  size_t tagLength = 0;
  size_t entityLength = 0;
  size_t anchorLength = 0;
  size_t outputLength = 0;
  size_t linkCount = 0;
};

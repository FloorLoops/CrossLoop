#pragma once

#include <cstddef>
#include <string_view>

namespace crossloop::web {
inline bool continuation(char c) { return (static_cast<unsigned char>(c) & 0xc0) == 0x80; }
inline size_t nextCharacter(std::string_view text, size_t at) {
  if (at >= text.size()) return text.size();
  ++at;
  while (at < text.size() && continuation(text[at])) ++at;
  return at;
}
// Keep a valid multibyte character for the next SD chunk instead of rendering half.
inline size_t completePrefix(std::string_view text) {
  if (text.empty()) return 0;
  size_t start = text.size() - 1;
  while (start && continuation(text[start])) --start;
  const unsigned char lead = text[start];
  const size_t bytes = lead >= 0xf0 && lead <= 0xf4   ? 4
                       : lead >= 0xe0 && lead <= 0xef ? 3
                       : lead >= 0xc2 && lead <= 0xdf ? 2
                                                      : 1;
  return text.size() - start < bytes ? start : text.size();
}
inline size_t prepareChunk(char* text, size_t length, bool finalChunk) {
  const size_t complete = completePrefix({text, length});
  if (!finalChunk) return complete;
  // Malformed EOF cannot be deferred to another read. Keep byte offsets stable
  // while presenting one replacement per incomplete trailing byte.
  for (size_t i = complete; i < length; ++i) text[i] = '?';
  return length;
}
struct LineSpan {
  size_t length;
  size_t consumed;
};
// Width callback measures a prefix in the selected font. Binary search replaces
// measuring every successively longer prefix; no line-sized scratch allocation.
template <typename Width>
LineSpan wrapLine(std::string_view text, int width, Width measure) {
  if (text.empty()) return {0, 0};
  if (text.front() == '\n') return {0, 1};
  size_t stop = text.find('\n');
  if (stop == std::string_view::npos) stop = text.size();
  size_t lo = 0, hi = stop;
  while (lo < hi) {
    size_t mid = lo + (hi - lo + 1) / 2;
    while (mid > lo && mid < text.size() && continuation(text[mid])) --mid;
    if (mid == lo) mid = nextCharacter(text, lo);
    if (measure(mid) <= width) {
      lo = mid;
    } else {
      hi = mid - 1;
      while (hi && continuation(text[hi])) --hi;
    }
  }
  size_t end = lo;
  if (!end) end = nextCharacter(text, 0);
  if (end < stop) {
    const size_t space = text.substr(0, end + (text[end] == ' ' ? 1 : 0)).find_last_of(' ');
    if (space != std::string_view::npos && space > 0) end = space;
  }
  size_t consumed = end;
  while (consumed < text.size() && text[consumed] == ' ') ++consumed;
  if (consumed < text.size() && text[consumed] == '\n') ++consumed;
  return {end, consumed};
}
}  // namespace crossloop::web

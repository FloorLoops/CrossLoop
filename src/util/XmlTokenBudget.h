#pragma once

#include <cstddef>
#include <cstring>

namespace crossloop {
// Bound Expat's incomplete-token allocation, including quoted '>' characters
// and comments/CDATA. Long text outside markup streams normally.
class XmlTokenBudget {
 public:
  bool feed(const char* bytes, size_t count) {
    for (size_t i = 0; i < count; ++i) {
      const char c = bytes[i];
      if (!length) {
        if (c != '<') continue;
        length = 1;
        prefix[0] = '<';
        prefix[1] = 0;
        mode = Mode::Tag;
        quote = 0;
        previous = beforePrevious = 0;
        continue;
      }
      if (++length > 4096) return false;
      if (length < sizeof(prefix)) {
        prefix[length - 1] = c;
        prefix[length] = 0;
      }
      if (length == 2 && !strcmp(prefix, "<?")) mode = Mode::Instruction;
      if (length == 4 && !strcmp(prefix, "<!--")) mode = Mode::Comment;
      if (length == 9 && !strcmp(prefix, "<![CDATA[")) mode = Mode::Cdata;
      if (length == 9 && !strcmp(prefix, "<!DOCTYPE")) return false;
      if (mode == Mode::Tag) {
        if (quote) {
          if (c == quote) quote = 0;
        } else if (c == '\'' || c == '"')
          quote = c;
        else if (c == '>')
          length = 0;
      } else if (c == '>') {
        if ((mode == Mode::Instruction && previous == '?') ||
            (mode == Mode::Comment && length >= 7 && previous == '-' && beforePrevious == '-') ||
            (mode == Mode::Cdata && previous == ']' && beforePrevious == ']'))
          length = 0;
      }
      beforePrevious = previous;
      previous = c;
    }
    return true;
  }

 private:
  enum class Mode { Tag, Instruction, Comment, Cdata } mode = Mode::Tag;
  size_t length = 0;
  char prefix[10]{};
  char quote = 0;
  char previous = 0;
  char beforePrevious = 0;
};
}  // namespace crossloop

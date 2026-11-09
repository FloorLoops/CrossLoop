#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace crossloop {
// The SDK SAX decoder supplies bounded decoded strings, but intentionally
// tolerates missing punctuation. Validate framing/grammar before committing
// any downloaded account snapshot. No values are retained here.
class JsonStructure {
 public:
  bool feed(const char* bytes, size_t byteCount) {
    for (size_t i = 0; i < byteCount && ok; ++i) consume(bytes[i]);
    return ok;
  }
  bool finish() {
    if (mode == Mode::Number) endNumber();
    return ok && mode == Mode::Idle && depth == 0 && rootDone;
  }

 private:
  enum class Mode : uint8_t { Idle, String, Escape, Unicode, Number, Literal };
  enum class Expect : uint8_t { ObjectFirst, Key, Colon, Value, ArrayFirst, ObjectAfter, ArrayAfter };
  Expect stack[32]{};
  unsigned depth = 0;
  Mode mode = Mode::Idle;
  bool ok = true;
  bool rootDone = false;
  bool key = false;
  unsigned digits = 0;
  char number[64]{};
  unsigned length = 0;
  const char* literal = nullptr;
  static bool digit(char c) { return c >= '0' && c <= '9'; }
  void valueDone() {
    if (!depth)
      rootDone = true;
    else if (stack[depth - 1] == Expect::Value)
      stack[depth - 1] = Expect::ObjectAfter;
    else if (stack[depth - 1] == Expect::ArrayFirst)
      stack[depth - 1] = Expect::ArrayAfter;
    else
      ok = false;
  }
  void push(Expect state) {
    if (depth == 32)
      ok = false;
    else
      stack[depth++] = state;
  }
  void endNumber() {
    size_t i = 0;
    if (number[i] == '-') ++i;
    if (number[i] == '0')
      ++i;
    else {
      if (number[i] < '1' || number[i] > '9') {
        ok = false;
        return;
      }
      while (digit(number[i])) ++i;
    }
    if (number[i] == '.') {
      ++i;
      if (!digit(number[i])) {
        ok = false;
        return;
      }
      while (digit(number[i])) ++i;
    }
    if (number[i] == 'e' || number[i] == 'E') {
      ++i;
      if (number[i] == '+' || number[i] == '-') ++i;
      if (!digit(number[i])) {
        ok = false;
        return;
      }
      while (digit(number[i])) ++i;
    }
    if (i != length) {
      ok = false;
      return;
    }
    mode = Mode::Idle;
    valueDone();
  }
  void consume(char c) {
    if (mode == Mode::Number) {
      if (digit(c) || c == '-' || c == '+' || c == '.' || c == 'e' || c == 'E') {
        if (length + 1 >= sizeof(number))
          ok = false;
        else {
          number[length++] = c;
          number[length] = 0;
        }
        return;
      }
      endNumber();
      if (!ok) return;
    }
    if (mode == Mode::Literal) {
      if (c != literal[length++]) {
        ok = false;
        return;
      }
      if (!literal[length]) {
        mode = Mode::Idle;
        valueDone();
      }
      return;
    }
    if (mode == Mode::Unicode) {
      if (!digit(c) && !(c >= 'a' && c <= 'f') && !(c >= 'A' && c <= 'F')) ok = false;
      if (++digits == 4) mode = Mode::String;
      return;
    }
    if (mode == Mode::Escape) {
      if (c == 'u') {
        mode = Mode::Unicode;
        digits = 0;
      } else if (c && strchr("\"\\/bfnrt", c))
        mode = Mode::String;
      else
        ok = false;
      return;
    }
    if (mode == Mode::String) {
      if (static_cast<unsigned char>(c) < 32)
        ok = false;
      else if (c == '\\')
        mode = Mode::Escape;
      else if (c == '"') {
        mode = Mode::Idle;
        if (key)
          stack[depth - 1] = Expect::Colon;
        else
          valueDone();
      }
      return;
    }
    if (c == ' ' || c == '\n' || c == '\r' || c == '\t') return;
    if (!depth && rootDone) {
      ok = false;
      return;
    }
    if (depth) {
      auto& state = stack[depth - 1];
      if (state == Expect::ObjectFirst || state == Expect::Key) {
        if (c == '}' && state == Expect::ObjectFirst) {
          --depth;
          valueDone();
        } else if (c == '"') {
          key = true;
          mode = Mode::String;
        } else
          ok = false;
        return;
      }
      if (state == Expect::Colon) {
        if (c == ':')
          state = Expect::Value;
        else
          ok = false;
        return;
      }
      if (state == Expect::ObjectAfter || state == Expect::ArrayAfter) {
        const bool object = state == Expect::ObjectAfter;
        if (c == (object ? '}' : ']')) {
          --depth;
          valueDone();
        } else if (c == ',')
          state = object ? Expect::Key : Expect::ArrayFirst;
        else
          ok = false;
        // ArrayFirst after a comma must not admit a closing bracket.
        if (!object && c == ',') arrayCommaDepth = depth;
        return;
      }
      if (state == Expect::ArrayFirst && c == ']') {
        if (arrayCommaDepth == depth) {
          ok = false;
          return;
        }
        --depth;
        valueDone();
        return;
      }
    }
    if (arrayCommaDepth == depth) arrayCommaDepth = 0;
    key = false;
    if (c == '{')
      push(Expect::ObjectFirst);
    else if (c == '[')
      push(Expect::ArrayFirst);
    else if (c == '"')
      mode = Mode::String;
    else if (c == '-' || digit(c)) {
      mode = Mode::Number;
      length = 1;
      number[0] = c;
      number[1] = 0;
    } else if (c == 't' || c == 'f' || c == 'n') {
      literal = c == 't' ? "true" : c == 'f' ? "false" : "null";
      length = 1;
      mode = Mode::Literal;
    } else
      ok = false;
  }
  unsigned arrayCommaDepth = 0;
};
}  // namespace crossloop

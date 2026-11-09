#include <iostream>
#include <string>

#include "util/JsonStructure.h"
#include "util/PersonalCache.h"
#include "util/TickTickParser.h"

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
void raw(const std::string& value) { fake::files["/raw"] = std::vector<uint8_t>(value.begin(), value.end()); }
bool structure(const std::string& value, size_t split) {
  crossloop::JsonStructure parser;
  return parser.feed(value.data(), split) && parser.feed(value.data() + split, value.size() - split) && parser.finish();
}
bool parse(const std::string& json, TickTickResponse kind = TickTickResponse::Tasks) {
  raw(json);
  CacheWriter writer;
  return writer.begin(kind == TickTickResponse::Tasks ? Dataset::Tasks : Dataset::Projects) &&
         parseTickTick("/raw", kind, "project1", writer) && writer.commit();
}
int main() {
  for (const std::string value :
       {"{}", "[]", "[{},[1,true,null],3]", "{\"k\":{\"a\":[false,\"\\u1234\"]}}", "0", "-0.1E+3", "\"a\\\\b\""}) {
    for (size_t split = 0; split <= value.size(); ++split) CHECK(structure(value, split));
  }
  for (const std::string value :
       {"",         "{",          "[",       "tru",         "{\"a\" 1}",   "[1,]", "{\"a\":1,}",
        "{\"a\":}", "[01]",       "[1 2]",   "[truefalse]", "{}{}",        "[1e]", "[1.]",
        "[+1]",     "[--1]",      "[.1]",    "[1e+-2]",     "{]",          "[}",   "\"\\u123\"",
        "\"\\x\"",  "\"unclosed", "[[] , ]", "[[],[2,]]",   "{\"a\":[1,]}"}) {
    for (size_t split = 0; split <= value.size(); ++split) CHECK(!structure(value, split));
  }
  CHECK(!structure(std::string(33, '[') + std::string(33, ']'), 0));
  CHECK(structure(std::string(32, '[') + std::string(32, ']'), 0));
  const std::string task =
      R"({"id":"task1","projectId":"project1","title":"Read a book","content":"Notes","isAllDay":true,"dueDate":"2026-11-08T00:00:00+0000","status":0,"items":[{"id":"subtask","title":"Ignore nested"}]})";
  const auto response = "{\"project\":{\"id\":\"project1\"},\"tasks\":[" + task + "]}";
  CHECK(parse(response));
  CacheReader reader;
  Item item;
  CHECK(reader.open(Dataset::Tasks));
  CHECK(reader.size() == 1);
  CHECK(reader.read(0, item));
  CHECK(std::string(item.title) == "Read a book");
  CHECK(item.flags == AllDay);
  CHECK(std::string(item.id) == "task1");
  CHECK(!reader.read(1, item));
  reader.close();
  const auto saved = fake::files[cachePath(Dataset::Tasks)];
  for (const std::string bad :
       {"{}", "[]", "{\"tasks\":[1]}", "{\"tasks\":[[]]}", "{\"tasks\":{}}", "{\"tasks\":[],\"tasks\":[]}",
        "{\"tasks\":[{\"id\":\"a/b\",\"projectId\":\"project1\",\"title\":\"X\"}]}",
        "{\"tasks\":[{\"id\":\"a\",\"projectId\":\"wrong\",\"title\":\"X\"}]}",
        "{\"tasks\":[{\"id\":\"a\",\"id\":\"b\",\"projectId\":\"project1\",\"title\":\"X\"}]}"}) {
    CHECK(!parse(bad));
    CHECK(fake::files[cachePath(Dataset::Tasks)] == saved);
  }
  for (size_t at = 0; at < response.size(); ++at) {
    CHECK(!parse(response.substr(0, at)));
    CHECK(fake::files[cachePath(Dataset::Tasks)] == saved);
  }
  // A long irrelevant body is bounded, but oversized IDs must never truncate.
  CHECK(parse("{\"tasks\":[{\"id\":\"a\",\"projectId\":\"project1\",\"title\":\"" + std::string(5000, 'x') + "\"}]}"));
  CHECK(reader.open(Dataset::Tasks) && reader.read(0, item));
  CHECK(strlen(item.title) == 192);
  reader.close();
  CHECK(!parse("{\"tasks\":[{\"id\":\"" + std::string(129, 'x') + "\",\"projectId\":\"project1\",\"title\":\"X\"}]}"));
  CHECK(parse(response));
  const auto stable = fake::files[cachePath(Dataset::Tasks)];
  fake::writesLeft = 30;
  CHECK(!parse(response));
  CHECK(fake::files[cachePath(Dataset::Tasks)] == stable);
  fake::writesLeft = -1;
  fake::renameCalls = 0;
  fake::renameFailure = 2;
  CHECK(!parse(response));
  CHECK(reader.open(Dataset::Tasks) && reader.read(0, item));
  CHECK(std::string(item.title) == "Read a book");
  reader.close();
  fake::renameFailure = 0;
  // A complete snapshot survives a missing/corrupt primary through .bak.
  fake::files[cachePath(Dataset::Tasks) + ".bak"] = stable;
  fake::files[cachePath(Dataset::Tasks)][30] ^= 1;
  CHECK(reader.open(Dataset::Tasks) && reader.read(0, item));
  CHECK(std::string(item.id) == "task1");
  reader.close();
  fake::files.erase(cachePath(Dataset::Tasks));
  CHECK(reader.open(Dataset::Tasks));
  reader.close();
  CHECK(parse("{\"tasks\":[]}"));
  CHECK(reader.open(Dataset::Tasks) && reader.size() == 0);
  reader.close();
  CHECK(
      parse(R"([{"id":"p1","name":"Reading"},{"id":"p2","name":"Closed","closed":true}])", TickTickResponse::Projects));
  CHECK(reader.open(Dataset::Projects) && reader.size() == 1 && reader.read(0, item));
  CHECK(std::string(item.title) == "Reading");
  reader.close();
  std::string huge = "{\"tasks\":[";
  for (unsigned i = 0; i <= MAX_RECORDS; ++i) {
    if (i) huge += ',';
    huge += task;
  }
  huge += "]}";
  CHECK(!parse(huge));
  CHECK(removeCache(Dataset::Tasks));
  CHECK(!reader.open(Dataset::Tasks));
  std::cout << checks << " personal data checks; " << failures << " failures\n";
  return failures ? 1 : 0;
}

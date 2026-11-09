#include "TickTickParser.h"

#include <Memory.h>
#include <StreamingJsonParser.h>

#include <algorithm>
#include <cstring>

#include "JsonStructure.h"

namespace crossloop::personal {
bool validTickTickId(const char* value) {
  const size_t length = strlen(value);
  if (!length || length > 128) return false;
  for (size_t i = 0; i < length; ++i) {
    const unsigned char c = value[i];
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-'))
      return false;
  }
  return true;
}
namespace {
enum class Field : uint8_t { Other, Id, Group, Title, Detail, When, AllDay, Status, Closed };
struct Context {
  Context(TickTickResponse type, CacheWriter& writer, const char* project)
      : type(type), writer(writer), project(project) {}
  TickTickResponse type;
  CacheWriter& writer;
  const char* project;
  Item item;
  char key[32]{};
  unsigned depth = 0;
  unsigned recordDepth = 0;
  unsigned arrayDepth = 0;
  uint16_t fields = 0;
  bool foundArray = false;
  bool closed = false;
  bool ok = true;
};
void key(void* user, const char* value, size_t length) {
  auto& self = *static_cast<Context*>(user);
  if (length >= sizeof(self.key))
    self.key[0] = 0;
  else {
    memcpy(self.key, value, length);
    self.key[length] = 0;
  }
}
Field field(Context& self) {
  if (self.depth != self.recordDepth || !self.recordDepth) return Field::Other;
  if (!strcmp(self.key, "id")) return Field::Id;
  if (!strcmp(self.key, "projectId")) return Field::Group;
  if (!strcmp(self.key, self.type == TickTickResponse::Projects ? "name" : "title")) return Field::Title;
  if (!strcmp(self.key, "content")) return Field::Detail;
  if (!strcmp(self.key, "dueDate")) return Field::When;
  if (!strcmp(self.key, "isAllDay")) return Field::AllDay;
  if (!strcmp(self.key, "status")) return Field::Status;
  if (!strcmp(self.key, "closed")) return Field::Closed;
  return Field::Other;
}
Field scalarField(Context& self) {
  if (!self.depth || (self.arrayDepth && self.depth == self.arrayDepth)) self.ok = false;
  return field(self);
}
bool once(Context& self, Field value) {
  if (value == Field::Other) return true;
  const uint16_t bit = 1u << static_cast<unsigned>(value);
  if (self.fields & bit) {
    self.ok = false;
    return false;
  }
  self.fields |= bit;
  return true;
}
void stringValue(void* user, const char* value, size_t length) {
  auto& self = *static_cast<Context*>(user);
  const auto selected = scalarField(self);
  if (!once(self, selected)) return;
  char* output = nullptr;
  size_t capacity = 0;
  bool truncate = false;
  switch (selected) {
    case Field::Id:
      output = self.item.id;
      capacity = sizeof(self.item.id);
      break;
    case Field::Group:
      output = self.item.group;
      capacity = sizeof(self.item.group);
      break;
    case Field::Title:
      output = self.item.title;
      capacity = sizeof(self.item.title);
      truncate = true;
      break;
    case Field::Detail:
      output = self.item.detail;
      capacity = sizeof(self.item.detail);
      truncate = true;
      break;
    case Field::When:
      output = self.item.when;
      capacity = sizeof(self.item.when);
      break;
    case Field::Other:
      return;
    default:
      self.ok = false;
      return;
  }
  if (memchr(value, 0, length) || (!truncate && length >= capacity)) {
    self.ok = false;
    return;
  }
  size_t copy = std::min(length, capacity - 1);
  if (copy < length)
    while (copy && (static_cast<unsigned char>(value[copy]) & 0xc0) == 0x80) --copy;
  memcpy(output, value, copy);
  output[copy] = 0;
  for (size_t i = 0; i < copy; ++i)
    if (static_cast<unsigned char>(output[i]) < 32) output[i] = ' ';
}
void prefix(void* user, const char* value, size_t length) {
  auto& self = *static_cast<Context*>(user);
  const auto selected = scalarField(self);
  if (selected == Field::Title || selected == Field::Detail)
    stringValue(user, value, length);
  else if (selected != Field::Other)
    self.ok = false;
}
void boolean(void* user, bool value) {
  auto& self = *static_cast<Context*>(user);
  const auto selected = scalarField(self);
  if (!once(self, selected)) return;
  if (selected == Field::AllDay) {
    if (value) self.item.flags |= AllDay;
  } else if (selected == Field::Closed)
    self.closed = value;
  else if (selected != Field::Other)
    self.ok = false;
}
void number(void* user, const char* value, size_t length) {
  auto& self = *static_cast<Context*>(user);
  const auto selected = scalarField(self);
  if (!once(self, selected)) return;
  if (selected == Field::Status && length == 1 && (value[0] == '0' || value[0] == '2')) {
    if (value[0] == '2') self.item.flags |= Completed;
  } else if (selected != Field::Other)
    self.ok = false;
}
void nullValue(void* user) {
  auto& self = *static_cast<Context*>(user);
  const auto selected = scalarField(self);
  if (!once(self, selected)) return;
  if (selected != Field::Other && selected != Field::When && selected != Field::Detail) self.ok = false;
}
void objectStart(void* user) {
  auto& self = *static_cast<Context*>(user);
  if (field(self) != Field::Other) self.ok = false;
  if (!self.depth && self.type != TickTickResponse::Tasks) self.ok = false;
  ++self.depth;
  if (self.arrayDepth && self.depth == self.arrayDepth + 1) {
    self.recordDepth = self.depth;
    self.item = {};
    self.fields = 0;
    self.closed = false;
  }
  self.key[0] = 0;
}
void objectEnd(void* user) {
  auto& self = *static_cast<Context*>(user);
  if (self.depth == self.recordDepth && self.recordDepth) {
    if (!validTickTickId(self.item.id) || !self.item.title[0]) self.ok = false;
    if (self.type == TickTickResponse::Tasks && strcmp(self.project, self.item.group)) self.ok = false;
    if (self.ok && !self.closed && !(self.item.flags & Completed)) self.ok = self.writer.append(self.item);
    self.recordDepth = 0;
  }
  if (self.depth) --self.depth;
}
void arrayStart(void* user) {
  auto& self = *static_cast<Context*>(user);
  if (field(self) != Field::Other || (self.arrayDepth && self.depth == self.arrayDepth)) self.ok = false;
  if (self.type == TickTickResponse::Projects && !self.depth) {
    self.arrayDepth = 1;
    self.foundArray = true;
  } else if (self.type == TickTickResponse::Tasks && self.depth == 1 && !strcmp(self.key, "tasks")) {
    if (self.foundArray) self.ok = false;
    self.arrayDepth = 2;
    self.foundArray = true;
  }
  ++self.depth;
  self.key[0] = 0;
}
void arrayEnd(void* user) {
  auto& self = *static_cast<Context*>(user);
  if (self.depth == self.arrayDepth) self.arrayDepth = 0;
  if (self.depth) --self.depth;
}
}  // namespace
bool parseTickTick(const char* path, TickTickResponse type, const char* projectId, CacheWriter& writer) {
  if (type == TickTickResponse::Tasks && (!projectId || !validTickTickId(projectId))) return false;
  auto context = makeUniqueNoThrow<Context>(type, writer, projectId);
  if (!context) return false;
  JsonCallbacks callbacks{context.get(), key,       stringValue, number,   boolean, nullValue,
                          objectStart,   objectEnd, arrayStart,  arrayEnd, prefix};
  auto parser = makeUniqueNoThrow<StreamingJsonParser>(callbacks);
  auto buffer = makeUniqueNoThrow<char[]>(1024);
  if (!parser || !buffer) return false;
  auto file = Storage.open(path);
  if (!file || file.size() > 512 * 1024) return false;
  JsonStructure structure;
  while (file.available()) {
    const int size = file.read(buffer.get(), 1024);
    if (size <= 0 || !structure.feed(buffer.get(), size)) return false;
    parser->feed(buffer.get(), size);
    if (!context->ok || parser->hasError()) return false;
  }
  parser->feed(" ", 1);
  return context->ok && context->foundArray && !context->depth && !parser->hasError() && structure.finish();
}
}  // namespace crossloop::personal

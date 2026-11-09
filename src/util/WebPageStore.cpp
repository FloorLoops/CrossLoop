#include "WebPageStore.h"

#include <Arduino.h>
#include <MD5Builder.h>
#include <Memory.h>

#include <cstring>
#include <ctime>

#include "BookCacheUtils.h"
#include "RecoverableFile.h"
#include "WebTextStream.h"
#include "network/WebUrl.h"

namespace crossloop::web {
namespace {
constexpr char MAGIC[8] = {'C', 'L', 'W', 'E', 'B', '0', '1', '\n'};

void put32(uint8_t* out, uint32_t value) {
  for (int i = 0; i < 4; ++i) out[i] = static_cast<uint8_t>(value >> (8 * i));
}
uint32_t get32(const uint8_t* in) {
  uint32_t value = 0;
  for (int i = 0; i < 4; ++i) value |= static_cast<uint32_t>(in[i]) << (8 * i);
  return value;
}
bool validKey(const std::string& key) {
  return key.size() == 32 && key.find_first_not_of("0123456789abcdef") == std::string::npos;
}
struct PageMetadata {
  uint32_t textBytes;
  uint32_t links;
  int64_t syncedAt;
};
bool readHeader(HalFile& file, char* url, PageMetadata& info) {
  uint8_t header[32];
  if (file.size() < BODY_OFFSET || file.read(header, sizeof(header)) != sizeof(header) ||
      memcmp(header, MAGIC, sizeof(MAGIC)))
    return false;
  info.textBytes = get32(header + 8);
  info.links = get32(header + 12);
  const uint64_t epoch = static_cast<uint64_t>(get32(header + 16)) | (static_cast<uint64_t>(get32(header + 20)) << 32);
  info.syncedAt = epoch >= 1735689600 && epoch < 4102444800 ? static_cast<int64_t>(epoch) : 0;
  if (info.textBytes > MAX_TEXT || info.links > MAX_LINKS || file.size() != BODY_OFFSET + info.textBytes ||
      file.read(url, 1025) != 1025 || url[1024] != 0)
    return false;
  return validUrl(url);
}
bool openStoredPage(const std::string& key, HalFile& file, char* url, PageMetadata& metadata) {
  const auto path = pagePath(key);
  if (path.empty()) return false;
  file = Storage.open(path.c_str());
  if (file && readHeader(file, url, metadata)) return true;
  file.close();
  file = Storage.open((path + ".bak").c_str());
  return file && readHeader(file, url, metadata);
}
struct ImportContext {
  HalFile output;
  std::string base;
  PageLink link;
  uint32_t textBytes = 0;
  uint32_t links = 0;
};
bool writeText(void* context, const char* text, size_t length) {
  auto& state = *static_cast<ImportContext*>(context);
  if (length > MAX_TEXT - state.textBytes || state.output.write(text, length) != length) return false;
  state.textBytes += static_cast<uint32_t>(length);
  return true;
}
bool writeLink(void* context, const char* target, const char* label) {
  auto& state = *static_cast<ImportContext*>(context);
  if (state.links >= MAX_LINKS) return false;
  const auto resolved = resolveUrl(state.base, target);
  state.link = {};
  snprintf(state.link.url, sizeof(state.link.url), "%s", resolved.c_str());
  snprintf(state.link.label, sizeof(state.link.label), "%s", label);
  const size_t textPosition = state.output.position();
  if (!state.output.seek(HEADER_BYTES + state.links * LINK_BYTES) ||
      state.output.write(state.link.url, sizeof(state.link.url)) != sizeof(state.link.url) ||
      state.output.write(state.link.label, sizeof(state.link.label)) != sizeof(state.link.label) ||
      !state.output.seek(textPosition))
    return false;
  ++state.links;
  return true;
}
}  // namespace

std::string pageKey(const std::string& url) {
  MD5Builder digest;
  digest.begin();
  digest.add(reinterpret_cast<const uint8_t*>(url.data()), url.size());
  digest.calculate();
  return digest.toString().c_str();
}
std::string pagePath(const std::string& key) {
  return validKey(key) ? std::string(CACHE_DIRECTORY) + "/" + key + ".web" : std::string{};
}
bool readPageInfo(const std::string& key, PageInfo& info) {
  HalFile file;
  return openPageFile(key, file, info);
}
bool openPageFile(const std::string& key, HalFile& file, PageInfo& info) {
  PageMetadata metadata{};
  if (!openStoredPage(key, file, info.url, metadata)) return false;
  info.links = metadata.links;
  info.textBytes = metadata.textBytes;
  info.syncedAt = metadata.syncedAt;
  return true;
}
bool readPageLink(const std::string& key, unsigned index, PageLink& link) {
  PageMetadata metadata{};
  HalFile file;
  if (!openStoredPage(key, file, link.url, metadata) || index >= metadata.links ||
      !file.seek(HEADER_BYTES + index * LINK_BYTES) || file.read(link.url, sizeof(link.url)) != sizeof(link.url) ||
      file.read(link.label, sizeof(link.label)) != sizeof(link.label))
    return false;
  link.url[1024] = link.label[127] = 0;
  return validUrl(link.url);
}

bool importPage(const std::string& rawPath, const std::string& url, bool plainText, std::string& key) {
  if (!validUrl(url) || !Storage.ensureDirectoryExists(CACHE_DIRECTORY)) return false;
  auto input = Storage.open(rawPath.c_str());
  if (!input || input.size() > 512 * 1024) return false;
  auto context = makeUniqueNoThrow<ImportContext>();
  auto buffer = makeUniqueNoThrow<char[]>(1024);
  if (!context || !buffer) return false;
  auto parser = makeUniqueNoThrow<WebTextStream>(WebTextStream::Sink{context.get(), writeText, writeLink}, plainText);
  if (!parser) return false;
  const std::string nextKey = pageKey(url);
  const std::string destination = pagePath(nextKey);
  const std::string temporary = destination + ".part";
  context->base = url;
  if (!Storage.openFileForWrite("WEB", temporary, context->output)) return false;
  ScopedCleanup cleanup{[&] {
    context->output.close();
    Storage.remove(temporary.c_str());
  }};
  // Reserve the fixed link index on SD; no in-memory page or link vector.
  for (size_t remaining = BODY_OFFSET; remaining;) {
    const size_t count = std::min(remaining, size_t{1024});
    if (context->output.write(buffer.get(), count) != count) return false;
    remaining -= count;
  }
  while (input.available()) {
    const int length = input.read(buffer.get(), 1024);
    if (length <= 0 || !parser->feed(buffer.get(), length)) return false;
    delay(1);
  }
  if (!parser->finish() || !context->textBytes) return false;
  uint8_t header[32]{};
  memcpy(header, MAGIC, sizeof(MAGIC));
  put32(header + 8, context->textBytes);
  put32(header + 12, context->links);
  const int64_t now = time(nullptr);
  put32(header + 16, static_cast<uint32_t>(now));
  put32(header + 20, static_cast<uint32_t>(now >> 32));
  if (!context->output.seek(0) || context->output.write(header, sizeof(header)) != sizeof(header)) return false;
  context->link = {};
  snprintf(context->link.url, sizeof(context->link.url), "%s", url.c_str());
  if (context->output.write(context->link.url, sizeof(context->link.url)) != sizeof(context->link.url) ||
      !context->output.close())
    return false;
  auto previous = Storage.open(destination.c_str());
  PageMetadata metadata{};
  const bool primaryValid = previous && readHeader(previous, context->link.url, metadata);
  previous.close();
  if (!publishRecoverableFile(temporary, destination, primaryValid)) return false;
  key = nextKey;
  return true;
}

bool exportPageText(const std::string& key, std::string& textPath) {
  auto info = makeUniqueNoThrow<PageInfo>();
  auto buffer = makeUniqueNoThrow<uint8_t[]>(1024);
  HalFile source;
  if (!info || !buffer || !openPageFile(key, source, *info) || !Storage.ensureDirectoryExists("/WebPages"))
    return false;
  const std::string destination = std::string("/WebPages/") + key + ".txt";
  const std::string temporary = destination + ".part";
  HalFile output;
  if (!source || !source.seek(BODY_OFFSET) || !Storage.openFileForWrite("WEB", temporary, output)) return false;
  ScopedCleanup cleanup{[&] {
    output.close();
    Storage.remove(temporary.c_str());
  }};
  size_t remaining = info->textBytes;
  while (remaining) {
    const size_t length = std::min(remaining, size_t{1024});
    if (source.read(buffer.get(), length) != static_cast<int>(length) || output.write(buffer.get(), length) != length)
      return false;
    remaining -= length;
  }
  if (!output.close() || !publishRecoverableFile(temporary, destination, Storage.exists(destination.c_str())))
    return false;
  clearBookCache(destination);
  textPath = destination;
  return true;
}
}  // namespace crossloop::web

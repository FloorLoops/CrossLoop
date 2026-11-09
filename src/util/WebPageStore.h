#pragma once

#include <HalStorage.h>

#include <cstdint>
#include <string>

namespace crossloop::web {
constexpr size_t MAX_LINKS = 32;
constexpr size_t MAX_TEXT = 256 * 1024;
constexpr size_t LINK_BYTES = 1025 + 128;
constexpr size_t HEADER_BYTES = 32 + 1025;
constexpr size_t BODY_OFFSET = HEADER_BYTES + MAX_LINKS * LINK_BYTES;
constexpr char CACHE_DIRECTORY[] = "/.crosspoint/web";

struct PageInfo {
  char url[1025]{};
  uint32_t textBytes = 0;
  uint32_t links = 0;
  int64_t syncedAt = 0;
};
struct PageLink {
  char url[1025]{};
  char label[128]{};
};

std::string pageKey(const std::string& url);
std::string pagePath(const std::string& key);
bool readPageInfo(const std::string& key, PageInfo& info);
bool openPageFile(const std::string& key, HalFile& file, PageInfo& info);
bool readPageLink(const std::string& key, unsigned index, PageLink& link);
bool importPage(const std::string& rawPath, const std::string& url, bool plainText, std::string& key);
bool exportPageText(const std::string& key, std::string& textPath);
}  // namespace crossloop::web

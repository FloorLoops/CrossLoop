#pragma once

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>

namespace crossloop::web {
constexpr size_t MAX_URL = 1024;

inline bool mediaTypeIs(std::string_view header, std::string_view expected) {
  header = header.substr(0, header.find(';'));
  while (!header.empty() && (header.front() == ' ' || header.front() == '\t')) header.remove_prefix(1);
  while (!header.empty() && (header.back() == ' ' || header.back() == '\t')) header.remove_suffix(1);
  return header.size() == expected.size() &&
         std::equal(header.begin(), header.end(), expected.begin(),
                    [](unsigned char a, unsigned char b) { return std::tolower(a) == std::tolower(b); });
}

inline bool validUrl(const std::string& url, bool requireHttps = false) {
  const size_t start = url.starts_with("https://") ? 8 : (!requireHttps && url.starts_with("http://") ? 7 : 0);
  if (!start || url.size() <= start || url.size() > MAX_URL) return false;
  const size_t end = url.find_first_of("/?#", start);
  const auto host = url.substr(start, end == std::string::npos ? end : end - start);
  if (host.empty() || host.find('@') != std::string::npos || host.find('%') != std::string::npos) return false;
  return std::none_of(url.begin(), url.end(), [](unsigned char c) { return c <= 32 || c == 127 || c == '\\'; });
}

inline std::string origin(const std::string& url) {
  if (!validUrl(url)) return {};
  const size_t end = url.find_first_of("/?#", url.find("://") + 3);
  std::string result = url.substr(0, end);
  std::transform(result.begin(), result.end(), result.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return result;
}

// RFC 3986 relative references used by links and Location headers. Reject
// unsupported schemes and credentials before handing a URL to the transport.
inline std::string resolveUrl(const std::string& base, std::string reference) {
  if (!validUrl(base) || reference.size() > MAX_URL) return {};
  const size_t fragment = reference.find('#');
  if (fragment != std::string::npos) reference.resize(fragment);
  std::string result;
  if (reference.starts_with("//")) {
    result = base.substr(0, base.find(':') + 1) + reference;
  } else if (reference.find(':') < reference.find_first_of("/?")) {
    result = reference;
  } else if (reference.empty()) {
    result = base.substr(0, base.find('#'));
  } else if (reference[0] == '/') {
    result = origin(base) + reference;
  } else if (reference[0] == '?') {
    result = base.substr(0, base.find_first_of("?#")) + reference;
  } else {
    const std::string root = origin(base);
    std::string path = base.substr(0, base.find_first_of("?#"));
    const size_t slash = path.rfind('/');
    path.resize(slash < root.size() ? root.size() : slash + 1);
    if (path == root) path += '/';
    result = path + reference;
  }
  if (!validUrl(result)) return {};
  const std::string root = origin(result);
  const size_t query = result.find('?', root.size());
  const std::string suffix = query == std::string::npos ? "" : result.substr(query);
  const std::string path = result.substr(root.size(), query == std::string::npos ? query : query - root.size());
  std::string normalized;
  size_t pos = 0;
  while (pos < path.size()) {
    const size_t end = path.find('/', pos + 1);
    const std::string part = path.substr(pos, end == std::string::npos ? end : end - pos);
    if (part == "/..") {
      const size_t slash = normalized.rfind('/');
      normalized.resize(slash == std::string::npos ? 0 : slash);
    } else if (part != "/.") {
      normalized += part;
    }
    if (end == std::string::npos) break;
    pos = end;
  }
  if (path.ends_with("/.") || path.ends_with("/..")) normalized += '/';
  result = root + (normalized.empty() ? "/" : normalized) + suffix;
  return validUrl(result) ? result : std::string{};
}
}  // namespace crossloop::web

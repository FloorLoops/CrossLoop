#pragma once

#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace personal_release {

// Set together in the release build. The key is public. Private signing
// material belongs outside the source tree, firmware, and SD card.
inline constexpr char repository[] = "FloorLoops/CrossLoop";
inline constexpr char publicKeyPem[] = R"PEM(-----BEGIN PUBLIC KEY-----
MFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAEwCZJLUZ+P8/gRS3MwnB7Xo4UYBEn
W/sA8jXb+BU2gqnD0GzonxDAyJYBxr+jpvvhgjnuP16uqqpETA0gDx/uQg==
-----END PUBLIC KEY-----
)PEM";

#if FREEINK_DEVICE_X4PRO
#define PERSONAL_RELEASE_MODEL "x4pro"
#else
#define PERSONAL_RELEASE_MODEL "x3"
#endif
inline constexpr char model[] = PERSONAL_RELEASE_MODEL;
inline constexpr char identityPrefix[] = "!XTEINK-PERSONAL-BUILD:" PERSONAL_RELEASE_MODEL ":";
#ifndef CROSSLOOP_RELEASE_VERSION
#define CROSSLOOP_RELEASE_VERSION "0.9.0-rc.3"
#endif
inline constexpr char buildIdentity[] = "!XTEINK-PERSONAL-BUILD:" PERSONAL_RELEASE_MODEL ":" CROSSLOOP_RELEASE_VERSION;
inline constexpr const char* version = buildIdentity + sizeof(identityPrefix) - 1;
#undef PERSONAL_RELEASE_MODEL

inline constexpr bool testingChannel = std::string_view(version).find("-rc.") != std::string_view::npos;
inline constexpr const char* manifestRoute = testingChannel ? "download/crossloop-testing/" : "latest/download/";

constexpr size_t maxManifestBytes = 512;
constexpr std::string_view magic = "XTEINK-PERSONAL-OTA-1";

struct Version {
  std::array<unsigned int, 3> numbers{};
  unsigned int releaseCandidate = 0;
};

inline bool parseVersion(std::string_view input, Version& parsed, bool allowRc) {
  parsed = {};
  for (size_t part = 0; part < parsed.numbers.size(); ++part) {
    if (input.empty()) return false;
    const auto begin = input.data();
    const auto end = begin + input.size();
    const auto [ptr, error] = std::from_chars(begin, end, parsed.numbers[part]);
    if (error != std::errc{} || (ptr - begin > 1 && *begin == '0')) return false;
    input.remove_prefix(ptr - begin);
    if (part < parsed.numbers.size() - 1) {
      if (!input.starts_with('.')) return false;
      input.remove_prefix(1);
    }
  }
  if (input.empty()) return true;
  if (!allowRc || !input.starts_with("-rc.")) return false;
  input.remove_prefix(4);
  if (input.empty() || input.front() == '0') return false;
  unsigned int rc = 0;
  const auto end = input.data() + input.size();
  const auto [ptr, error] = std::from_chars(input.data(), end, rc);
  if (error != std::errc{} || ptr != end || rc == 0) return false;
  parsed.releaseCandidate = rc;
  return true;
}

inline bool newerRelease(std::string_view candidate, std::string_view installed, bool allowRc = testingChannel) {
  Version next, current;
  if (!parseVersion(candidate, next, allowRc) || !parseVersion(installed, current, true)) return false;
  if (next.numbers != current.numbers) return next.numbers > current.numbers;
  if (current.releaseCandidate == 0) return false;
  return next.releaseCandidate == 0 || next.releaseCandidate > current.releaseCandidate;
}

inline bool validRepository(std::string_view name) {
  const size_t separator = name.find('/');
  if (separator == name.npos || name.size() > 120) return false;
  const auto validPart = [](std::string_view part) {
    return !part.empty() && part != "." && part != ".." &&
           part.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_.") == part.npos;
  };
  return validPart(name.substr(0, separator)) && validPart(name.substr(separator + 1));
}

struct Manifest {
  std::string_view version;
  size_t size = 0;
  std::array<uint8_t, 32> digest{};
  std::array<uint8_t, 80> signature{};
  size_t signatureSize = 0;
  size_t signedSize = 0;
};

// The prefix starts with '!' and no other part of the marker contains it,
// so matching can restart on that byte when a candidate prefix fails.
class IdentityScanner {
 public:
  explicit IdentityScanner(std::string_view expected) : expected(expected) {}

  void feed(const uint8_t* data, size_t len) {
    for (size_t i = 0; i < len && !matches(); ++i) {
      const char c = static_cast<char>(data[i]);
      matched = c == expected[matched] ? matched + 1 : (c == expected.front() ? 1 : 0);
    }
  }

  bool matches() const { return matched == expected.size(); }

 private:
  std::string_view expected;
  size_t matched = 0;
};

inline bool decodeHex(std::string_view input, uint8_t* output, size_t count) {
  if (input.size() != count * 2) return false;
  const auto nibble = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
  };
  for (size_t i = 0; i < count; ++i) {
    const int hi = nibble(input[2 * i]);
    const int lo = nibble(input[2 * i + 1]);
    if (hi < 0 || lo < 0) return false;
    output[i] = static_cast<uint8_t>((hi << 4) | lo);
  }
  return true;
}

inline bool parseManifest(std::string_view bytes, std::string_view expectedModel, Manifest& result) {
  if (bytes.empty() || bytes.size() > maxManifestBytes) return false;
  auto remaining = bytes;
  const auto line = [&remaining]() -> std::string_view {
    const size_t end = remaining.find('\n');
    if (end == remaining.npos) return {};
    const auto value = remaining.substr(0, end);
    remaining.remove_prefix(end + 1);
    return value;
  };
  if (line() != magic || line() != expectedModel) return false;
  result.version = line();
  Version parsed;
  if (result.version.size() > 30 || !parseVersion(result.version, parsed, testingChannel)) return false;
  const auto sizeText = line();
  if (sizeText.empty() || sizeText.front() == '0') return false;
  const auto sizeEnd = sizeText.data() + sizeText.size();
  const auto [ptr, error] = std::from_chars(sizeText.data(), sizeEnd, result.size);
  if (error != std::errc{} || ptr != sizeEnd || result.size < 24) return false;
  if (!decodeHex(line(), result.digest.data(), result.digest.size())) return false;
  result.signedSize = bytes.size() - remaining.size();
  const auto signature = line();
  result.signatureSize = signature.size() / 2;
  return remaining.empty() && result.signatureSize >= 8 && result.signatureSize <= result.signature.size() &&
         decodeHex(signature, result.signature.data(), result.signatureSize);
}

}  // namespace personal_release

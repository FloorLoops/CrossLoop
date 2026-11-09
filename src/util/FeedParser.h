#pragma once

#include "PersonalCache.h"

namespace crossloop::personal {
// RSS 2.0 and Atom 1.0 article links. The writer remains unpublished until all
// configured feeds have parsed. External entities/DTDs are not accepted.
bool parseFeed(const char* path, const std::string& requestUrl, CacheWriter& writer);
}  // namespace crossloop::personal

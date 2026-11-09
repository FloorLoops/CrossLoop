#pragma once

#include "PersonalCache.h"

namespace crossloop::personal {
enum class TickTickResponse { Projects, Tasks };
// Validates complete JSON and streams bounded records into an unpublished
// snapshot. Call commit only after every selected project's response passed.
bool parseTickTick(const char* path, TickTickResponse type, const char* projectId, CacheWriter& writer);
bool validTickTickId(const char* value);
}  // namespace crossloop::personal

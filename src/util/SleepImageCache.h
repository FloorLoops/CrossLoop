#pragma once

#include <string>

class GfxRenderer;
namespace crossloop {
bool sleepOriginalThresholds(const GfxRenderer& renderer);
// Convert before applying the sleep selection. The same cache is reused on
// sleep entry. JPEG/PNG decoders already enforce 2048x3072 source limits.
bool prepareSleepImage(const std::string& source, bool crop, bool thresholds, std::string& prepared,
                       bool force = false);
}  // namespace crossloop

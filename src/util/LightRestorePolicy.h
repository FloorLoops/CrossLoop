#pragma once

#include <cstdint>

namespace crossloop {

constexpr bool restoreLight(uint8_t mode, bool savedOn, bool sleepWake, bool sleepLightOn, int64_t sleepEpoch,
                            int64_t now, uint8_t windowMinutes) {
  if (mode == 1) return savedOn;
  if (mode != 2 || !sleepWake || !sleepLightOn || windowMinutes < 1 || windowMinutes > 60) return false;
  constexpr int64_t MIN_EPOCH = 1735689600LL;
  constexpr int64_t MAX_EPOCH = 4102444800LL;
  if (sleepEpoch < MIN_EPOCH || sleepEpoch >= MAX_EPOCH || now < sleepEpoch || now >= MAX_EPOCH) return false;
  return now - sleepEpoch <= static_cast<int64_t>(windowMinutes) * 60;
}

}  // namespace crossloop

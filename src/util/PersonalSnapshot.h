#pragma once

class GfxRenderer;
namespace crossloop::personal {
// Uses exactly the task cache shown by the native app. No network work is
// performed on sleep entry. Caller owns the final display refresh.
void drawTickTickSnapshot(GfxRenderer& renderer);
void drawDailySnapshot(GfxRenderer& renderer, bool word);
}  // namespace crossloop::personal

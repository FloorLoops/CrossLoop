#pragma once

#include "WeatherData.h"

class GfxRenderer;
namespace crossloop::personal {
// Portrait and wide panels; no file reads, decoder allocations or refresh.
void drawWeatherPanel(GfxRenderer& renderer, const WeatherData& data, int x, int y, int width);
void drawTodaySnapshot(GfxRenderer& renderer);
}  // namespace crossloop::personal

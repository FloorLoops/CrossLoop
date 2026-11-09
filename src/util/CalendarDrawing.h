#pragma once

#include "PersonalCache.h"

class GfxRenderer;
namespace crossloop::calendar {
void formatEventWhen(const personal::Item& item, char* output, size_t size);
// Small cached preview only; never opens or scans the full calendar at sleep.
void drawCalendarPreview(GfxRenderer& renderer, int x, int y, int width, int height);
}  // namespace crossloop::calendar

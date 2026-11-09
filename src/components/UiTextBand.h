#pragma once

#include <FreeInkApp.h>

// Reserve text above a list instead of centeredText(), which overlays the
// remaining body and does not consume any layout space.
template <typename Screen>
void uiTextBand(Screen& screen, const char* text, freeink::ui::TextStyle style, uint8_t lines = 1) {
  style.maxLines = lines;
  const auto height = static_cast<int16_t>(screen.frame().target().lineHeight(style.font) * lines + 4);
  screen.frame().target().text(screen.takeTop(height), text, style);
}

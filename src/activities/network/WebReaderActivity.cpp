#include "WebReaderActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Memory.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "components/UITheme.h"
#include "fontIds.h"
#include "util/WebPageStore.h"
#include "util/WebTextLayout.h"

namespace fui = freeink::ui;
namespace {
constexpr int TOP = 64;
constexpr int BOTTOM = 96;
constexpr int SIDE = 20;
constexpr fui::ActionId LINKS = 1;
constexpr fui::ActionId PREVIOUS = 2;
constexpr fui::ActionId NEXT = 3;
constexpr fui::ActionId FOLLOW = 4;
}  // namespace

int WebReaderActivity::textWidth(size_t start, size_t end) {
  const char saved = text[end];
  text[end] = 0;
  const int width = renderer.getTextWidth(UI_12_FONT_ID, text + start);
  text[end] = saved;
  return width;
}
bool WebReaderActivity::loadPage() {
  lineCount = 0;
  if (!file || offsets[currentPage] > textBytes || !file.seek(bodyOffset + offsets[currentPage])) return false;
  const size_t wanted = std::min<size_t>(sizeof(text) - 1, textBytes - offsets[currentPage]);
  const int read = file.read(text, wanted);
  if (read < 0 || static_cast<size_t>(read) != wanted) return false;
  const size_t length = crossloop::web::prepareChunk(text, wanted, offsets[currentPage] + wanted == textBytes);
  text[length] = 0;
  const int width = renderer.getScreenWidth() - 2 * SIDE;
  const int height = std::max(1, renderer.getLineHeight(UI_12_FONT_ID) + 4);
  const unsigned maxLines =
      std::min<unsigned>(lines.size(), std::max(0, renderer.getScreenHeight() - TOP - BOTTOM) / height);
  size_t pos = 0;
  while (pos < length && lineCount < maxLines) {
    const auto span = crossloop::web::wrapLine(std::string_view(text + pos, length - pos), width,
                                               [this, pos](size_t n) { return textWidth(pos, pos + n); });
    if (!span.consumed) return false;
    lines[lineCount++] = {static_cast<uint16_t>(pos), static_cast<uint16_t>(span.length)};
    pos += span.consumed;
  }
  nextOffset = offsets[currentPage] + pos;
  return wanted == 0 || pos > 0;
}
void WebReaderActivity::onEnter() {
  Activity::onEnter();
  resetUi();
  app.on(LINKS, action, this);
  app.on(PREVIOUS, action, this);
  app.on(NEXT, action, this);
  app.on(FOLLOW, action, this);
  app.setScreen(controls, this);
  auto info = makeUniqueNoThrow<crossloop::web::PageInfo>();
  failed = !info || !crossloop::web::openPageFile(key, file, *info);
  if (!info)
    LOG_ERR("WEB", "Page metadata allocation failed: %u bytes",
            static_cast<unsigned>(sizeof(crossloop::web::PageInfo)));
  linkOffsets.fill(UINT32_MAX);
  if (!failed) {
    textBytes = info->textBytes;
    bodyOffset = info->bodyOffset;
    linkCount = static_cast<uint8_t>(info->links);
    auto link = makeUniqueNoThrow<crossloop::web::PageLink>();
    if (!link) {
      LOG_ERR("WEB", "Link metadata allocation failed: %u bytes",
              static_cast<unsigned>(sizeof(crossloop::web::PageLink)));
    } else {
      for (unsigned i = 0; i < linkCount; ++i)
        if (crossloop::web::readPageLink(file, *info, i, *link)) linkOffsets[i] = link->textOffset;
    }
  }
  {
    RenderLock lock(*this);
    if (!failed) failed = !loadPage();
  }
  requestUpdate();
}
void WebReaderActivity::onExit() {
  closeRouting();
  if (file) file.close();
  Activity::onExit();
}
void WebReaderActivity::turn(int direction) {
  RenderLock lock(*this);
  if (failed || (direction < 0 && currentPage == 0) ||
      (direction > 0 && (nextOffset >= textBytes || currentPage + 1 >= offsets.size())))
    return;
  closeRouting();
  if (direction > 0)
    offsets[++currentPage] = nextOffset;
  else
    --currentPage;
  failed = !loadPage();
  requestUpdate();
}
void WebReaderActivity::action(const fui::ActionEvent& event, void* user) {
  auto& self = *static_cast<WebReaderActivity*>(user);
  self.app.clearTapFlash();
  if (event.action == LINKS)
    finish();
  else if (event.action == FOLLOW && event.value >= 0 && event.value < self.linkCount) {
    self.closeRouting();
    self.setResult(WebLinkResult{static_cast<unsigned>(event.value)});
    finish();
  } else if (event.action == PREVIOUS || event.action == NEXT) {
    self.turn(event.action == PREVIOUS ? -1 : 1);
  }
}
void WebReaderActivity::loop() {
  const auto touch = routeTouch(mappedInput);
  if (touch.routed && app.invalidated()) requestUpdate();
  if (touch) return;
  if (mappedInput.wasReleased(MappedInputManager::Button::Back) ||
      mappedInput.wasReleased(MappedInputManager::Button::Confirm))
    finish();
  else if (mappedInput.wasReleased(MappedInputManager::Button::NavNext))
    turn(1);
  else if (mappedInput.wasReleased(MappedInputManager::Button::NavPrevious))
    turn(-1);
}
void WebReaderActivity::controls(UiScreen& screen, void* user) {
  auto& self = *static_cast<WebReaderActivity*>(user);
  // Only indexed markers become links; literal "[1]" in article text is inert.
  // 20 inline targets + 3 footer controls fit the shared 24-entry routing table.
  unsigned targets = 0;
  const int lineHeight = self.renderer.getLineHeight(UI_12_FONT_ID) + 4;
  for (unsigned i = 0; !self.failed && i < self.linkCount && targets < 20; ++i) {
    const uint32_t absolute = self.linkOffsets[i];
    if (absolute == UINT32_MAX || absolute < self.offsets[self.currentPage]) continue;
    const uint32_t relative = absolute - self.offsets[self.currentPage];
    char marker[8];
    const int length = snprintf(marker, sizeof(marker), "[%u]", i + 1);
    for (unsigned row = 0; row < self.lineCount; ++row) {
      const auto& line = self.lines[row];
      if (relative < line.offset || relative + length > line.offset + line.length ||
          memcmp(self.text + relative, marker, length))
        continue;
      const int x = SIDE + self.textWidth(line.offset, relative);
      const int w = self.textWidth(relative, relative + length);
      const int y = TOP + row * lineHeight;
      screen.frame().hit(fui::Rect{static_cast<int16_t>(x), static_cast<int16_t>(y), static_cast<int16_t>(w),
                                   static_cast<int16_t>(lineHeight)},
                         FOLLOW, static_cast<int16_t>(i), fui::InputTouch);
      self.renderer.drawLine(x, y + lineHeight - 2, x + w, y + lineHeight - 2, 1, true);
      ++targets;
      break;
    }
  }
  const int width = self.renderer.getScreenWidth();
  const int y = self.renderer.getScreenHeight() - 52;
  const int third = (width - 2 * SIDE) / 3;
  const char* labels[] = {tr(STR_WEB_LINKS), tr(STR_PREV_PAGE), tr(STR_NEXT_PAGE)};
  const fui::ActionId actions[] = {LINKS, PREVIOUS, NEXT};
  for (int i = 0; i < 3; ++i) {
    fui::ButtonProps button;
    button.label = labels[i];
    button.action = actions[i];
    button.inputMask = fui::InputTouch;
    button.enabled =
        i == 0 ||
        (!self.failed && (i == 1 ? self.currentPage > 0
                                 : self.nextOffset < self.textBytes && self.currentPage + 1 < self.offsets.size()));
    button.text = screen.theme().bodyText;
    fui::button(
        screen.frame(),
        fui::Rect{static_cast<int16_t>(SIDE + i * third), static_cast<int16_t>(y), static_cast<int16_t>(third - 4), 42},
        button);
  }
}
void WebReaderActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, renderer.getScreenWidth(), metrics.headerHeight},
                 tr(STR_TEXT_WEB));
  if (failed)
    renderer.drawText(UI_12_FONT_ID, SIDE, TOP, tr(STR_FILE_OPEN_FAILED));
  else {
    const int height = renderer.getLineHeight(UI_12_FONT_ID) + 4;
    for (unsigned i = 0; i < lineCount; ++i) {
      const auto& line = lines[i];
      const size_t end = line.offset + line.length;
      const char saved = text[end];
      text[end] = 0;
      renderer.drawText(UI_12_FONT_ID, SIDE, TOP + i * height, text + line.offset);
      text[end] = saved;
    }
    char position[96];
    snprintf(position, sizeof(position), tr(STR_WEB_PAGE_NUMBER), static_cast<unsigned>(currentPage + 1));
    const bool limitReached = nextOffset < textBytes && currentPage + 1 >= offsets.size();
    renderer.drawText(UI_10_FONT_ID, SIDE, renderer.getScreenHeight() - 82,
                      limitReached ? tr(STR_WEB_PAGE_LIMIT) : position);
  }
  renderUi();
  renderer.displayBuffer();
}

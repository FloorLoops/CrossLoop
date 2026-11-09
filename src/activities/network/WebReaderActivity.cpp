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

namespace fui = freeink::ui;
namespace {
constexpr int TOP = 64;
constexpr int BOTTOM = 96;
constexpr int SIDE = 20;
constexpr fui::ActionId LINKS = 1;
constexpr fui::ActionId PREVIOUS = 2;
constexpr fui::ActionId NEXT = 3;
}  // namespace

size_t WebReaderActivity::nextCharacter(const char* value, size_t at, size_t length) {
  if (at >= length) return length;
  ++at;
  while (at < length && (static_cast<unsigned char>(value[at]) & 0xc0) == 0x80) ++at;
  return at;
}
int WebReaderActivity::textWidth(size_t start, size_t end) {
  const char saved = text[end];
  text[end] = 0;
  const int width = renderer.getTextWidth(UI_12_FONT_ID, text + start);
  text[end] = saved;
  return width;
}
bool WebReaderActivity::loadPage() {
  lineCount = 0;
  if (!file || !file.seek(crossloop::web::BODY_OFFSET + offsets[currentPage])) return false;
  const int read = file.read(text, sizeof(text) - 1);
  if (read < 0) return false;
  const size_t length = static_cast<size_t>(read);
  text[length] = 0;
  const int width = renderer.getScreenWidth() - 2 * SIDE;
  const int height = std::max(1, renderer.getLineHeight(UI_12_FONT_ID) + 4);
  const unsigned maxLines = std::min<unsigned>(lines.size(), (renderer.getScreenHeight() - TOP - BOTTOM) / height);
  size_t pos = 0;
  while (pos < length && lineCount < maxLines) {
    if (text[pos] == '\n') {
      lines[lineCount++] = {static_cast<uint16_t>(pos), 0};
      ++pos;
      continue;
    }
    const size_t start = pos;
    size_t fit = start;
    size_t space = start;
    while (pos < length && text[pos] != '\n') {
      const size_t end = nextCharacter(text, pos, length);
      if (text[pos] == ' ') space = pos;
      if (textWidth(start, end) > width) break;
      fit = end;
      pos = end;
    }
    size_t end = pos;
    if (pos < length && text[pos] != '\n') {
      end = space > start ? space : fit > start ? fit : nextCharacter(text, start, length);
    }
    if (end == start) end = nextCharacter(text, start, length);
    lines[lineCount++] = {static_cast<uint16_t>(start), static_cast<uint16_t>(end - start)};
    pos = end;
    while (pos < length && text[pos] == ' ') ++pos;
    if (pos < length && text[pos] == '\n') ++pos;
  }
  nextOffset = offsets[currentPage] + pos;
  return length == 0 || pos > 0;
}
void WebReaderActivity::onEnter() {
  Activity::onEnter();
  resetUi();
  app.on(LINKS, action, this);
  app.on(PREVIOUS, action, this);
  app.on(NEXT, action, this);
  app.setScreen(controls, this);
  auto info = makeUniqueNoThrow<crossloop::web::PageInfo>();
  failed = !info || !crossloop::web::openPageFile(key, file, *info);
  if (!failed) textBytes = info->textBytes;
  {
    RenderLock lock(*this);
    renderer.setOrientation(GfxRenderer::Orientation::Portrait);
    if (!failed) failed = !loadPage();
  }
  requestUpdate();
}
void WebReaderActivity::onExit() {
  closeRouting();
  file.close();
  Activity::onExit();
}
void WebReaderActivity::turn(int direction) {
  RenderLock lock(*this);
  if (failed || (direction < 0 && currentPage == 0) ||
      (direction > 0 && (nextOffset >= textBytes || currentPage + 1 >= offsets.size())))
    return;
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
  else
    self.turn(event.action == PREVIOUS ? -1 : 1);
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

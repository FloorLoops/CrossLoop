#include "BrowserMenuActivity.h"
#if CROSSLOOP_NETSURF_PROBE
#include <I18n.h>

#include "components/UITheme.h"
#include "components/UiTextBand.h"

namespace fui = freeink::ui;
BrowserMenuActivity::BrowserMenuActivity(GfxRenderer& renderer, MappedInputManager& input, std::string address,
                                         bool ready, bool forward, bool editable, bool saved, unsigned zoom)
    : UiListActivity("BrowserMenu", renderer, input), address(std::move(address)) {
  const StrId labels[] = {
      StrId::STR_WEB_OPEN_URL,           StrId::STR_DREAM_SEARCH_GOOGLE,
      StrId::STR_DREAM_SEARCH_WIKIPEDIA, saved ? StrId::STR_DREAM_REMOVE_BOOKMARK : StrId::STR_DREAM_SAVE_BOOKMARK,
      StrId::STR_DREAM_BOOKMARKS,        StrId::STR_DREAM_FORWARD,
      StrId::STR_DREAM_ZOOM_IN,          StrId::STR_DREAM_ZOOM_OUT,
      StrId::STR_DREAM_ZOOM_RESET,       StrId::STR_DREAM_FIELD_TEXT,
      StrId::STR_DREAM_SUBMIT_FIELD,     StrId::STR_WEB_RELOAD,
      StrId::STR_DREAM_CLOSE_BROWSER};
  for (int i = 0; i < Count; ++i) {
    rows[i].label = I18N.get(labels[i]);
    rows[i].actionValue = static_cast<int16_t>(i);
    rows[i].enabled = i <= Wikipedia || i == Bookmarks || i == Close || ready;
  }
  rows[Forward].enabled = ready && forward;
  rows[ZoomIn].enabled = ready && zoom < 200;
  rows[ZoomOut].enabled = ready && zoom > 75;
  rows[TypeField].enabled = rows[SubmitField].enabled = ready && editable;
}
const char* BrowserMenuActivity::headerTitle() const { return tr(STR_NATIVE_BROWSER); }
void BrowserMenuActivity::activateIndex(int index) {
  if (index < 0 || index >= Count || !rows[index].enabled) return;
  app.clearTapFlash();
  setResult(MenuResult{index});
  finish();
}
void BrowserMenuActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                                static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  uiTextBand(screen, address.empty() ? tr(STR_WEB_OPEN_URL) : address.c_str(), screen.theme().smallText, 3);
  fui::ListProps props;
  props.items = rows;
  props.count = Count;
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;
  syncListViewport(screen, props);
  screen.list(props);
}
#endif

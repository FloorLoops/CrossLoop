#include "SavedItemsActivity.h"
#if CROSSLOOP_DREAM_OS || CROSSLOOP_NETSURF_PROBE
#include <HalStorage.h>
#include <I18n.h>
#include <Memory.h>

#include <new>

#include "activities/util/ConfirmationActivity.h"
#include "components/UITheme.h"
#include "components/UiTextBand.h"

namespace fui = freeink::ui;
namespace saved = crossloop::saved;
saved::Table* SavedItemsActivity::table() const { return reinterpret_cast<saved::Table*>(data.get()); }
const char* SavedItemsActivity::headerTitle() const {
  return kind == saved::Kind::Book ? tr(STR_DREAM_PINNED_BOOKS) : tr(STR_DREAM_BOOKMARKS);
}
void SavedItemsActivity::onEnter() {
  UiListActivity::onEnter();
  reload();
}
void SavedItemsActivity::onExit() {
  closeRouting();
  count = 0;
  data.reset();
  Activity::onExit();
}
void SavedItemsActivity::reload() {
  RenderLock lock(*this);
  closeRouting();
  count = 0;
  if (!data) {
    data = HalMemory::allocatePsram(sizeof(saved::Table));
    if (data) new (data.get()) saved::Table;
  }
  failed = !data || !saved::load(*table());
  if (!failed)
    for (unsigned i = 0; i < table()->count; ++i) {
      const auto& entry = table()->entries[i];
      if (entry.kind != kind) continue;
      ordinals[count] = static_cast<uint8_t>(i);
      rows[count] = {};
      rows[count].label = entry.title;
      rows[count].subtitle = entry.target;
      rows[count].actionValue = static_cast<int16_t>(count);
      ++count;
    }
  nav.reset();
  requestUpdate();
}
void SavedItemsActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                                static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  uiTextBand(screen,
             failed                      ? tr(STR_DREAM_SAVED_FAILED)
             : count                     ? tr(STR_DREAM_SAVED_HELP)
             : kind == saved::Kind::Book ? tr(STR_DREAM_PINS_EMPTY)
                                         : tr(STR_DREAM_BOOKMARKS_EMPTY),
             screen.theme().smallText, 3);
  fui::ListProps props;
  props.items = rows;
  props.count = count;
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch | fui::InputLongPress;
  props.labelText = screen.theme().bodyText;
  props.labelText.maxLines = 2;
  props.subtitleText = screen.theme().smallText;
  props.subtitleText.maxLines = 1;
  syncListViewport(screen, props);
  screen.list(props);
}
void SavedItemsActivity::activateIndex(int index) {
  if (failed || !data || index < 0 || index >= count) return;
  app.clearTapFlash();
  const std::string target = table()->entries[ordinals[index]].target;
  if (kind == saved::Kind::Book) {
    auto file = Storage.open(target.c_str());
    if (!file || file.isDirectory()) {
      RenderLock lock(*this);
      GUI.drawPopup(renderer, tr(STR_DREAM_BOOK_MISSING));
      return;
    }
    file.close();
    onSelectBook(target);
  } else {
    setResult(WebAddressResult{target});
    finish();
  }
}
void SavedItemsActivity::onRowLongPress(int index) {
  if (failed || !data || index < 0 || index >= count) return;
  app.clearTapFlash();
  const std::string target = table()->entries[ordinals[index]].target;
  auto confirmation = makeUniqueNoThrow<ConfirmationActivity>(renderer, mappedInput, tr(STR_DREAM_REMOVE_SAVED),
                                                              tr(STR_DREAM_REMOVE_SAVED_CONFIRM));
  if (!confirmation) return;
  startActivityForResult(std::move(confirmation), [this, target](const ActivityResult& result) {
    if (result.isCancelled) return;
    bool savedOk = false;
    {
      RenderLock lock(*this);
      closeRouting();
      if (saved::load(*table())) {
        const int found = saved::find(*table(), kind, target);
        savedOk = found >= 0 && saved::erase(*table(), found) && saved::save(*table());
      }
    }
    reload();
    if (!savedOk) {
      RenderLock lock(*this);
      failed = true;
      requestUpdate();
    }
  });
}
bool SavedItemsActivity::handleButtons() {
  if (mappedInput.wasLongPressed(MappedInputManager::Button::Confirm, 1000)) {
    onRowLongPress(nav.selected);
    return true;
  }
  return UiListActivity::handleButtons();
}
#endif

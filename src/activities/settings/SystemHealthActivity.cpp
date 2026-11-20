#include "SystemHealthActivity.h"
#if CROSSLOOP_DREAM_OS
#include <HalMemory.h>
#include <HalStorage.h>
#include <I18n.h>
#include <WiFi.h>

#include "components/UITheme.h"
#include "components/UiTextBand.h"
#include "network/PersonalRelease.h"

namespace fui = freeink::ui;
const char* SystemHealthActivity::headerTitle() const { return tr(STR_DREAM_HEALTH); }
void SystemHealthActivity::onEnter() {
  UiListActivity::onEnter();
  sample();
}
void SystemHealthActivity::sample() {
  RenderLock lock(*this);
  const auto internal = HalMemory::getInternalHeap();
  const auto psram = HalMemory::getPsramHeap();
  snprintf(values[0], sizeof(values[0]), "%s", CROSSLOOP_RELEASE_VERSION);
  snprintf(values[1], sizeof(values[1]), tr(STR_DREAM_UPTIME_VALUE), static_cast<unsigned long>(millis() / 60000));
  snprintf(values[2], sizeof(values[2]), tr(STR_DREAM_MEMORY_VALUE), static_cast<unsigned>(internal.freeBytes / 1024),
           static_cast<unsigned>(internal.largestBlockBytes / 1024));
  snprintf(values[3], sizeof(values[3]), tr(STR_DREAM_MEMORY_VALUE), static_cast<unsigned>(psram.freeBytes / 1024),
           static_cast<unsigned>(psram.largestBlockBytes / 1024));
  snprintf(values[4], sizeof(values[4]), "%s", Storage.ready() ? tr(STR_DREAM_AVAILABLE) : tr(STR_DREAM_UNAVAILABLE));
  snprintf(values[5], sizeof(values[5]), "%s",
           WiFi.status() == WL_CONNECTED ? tr(STR_DREAM_CONNECTED) : tr(STR_DREAM_OFFLINE));
  snprintf(values[6], sizeof(values[6]), "%s",
           Storage.exists("/crash_report.txt") ? tr(STR_DREAM_CRASH_PRESENT) : tr(STR_DREAM_CRASH_ABSENT));
  constexpr StrId labels[] = {StrId::STR_DREAM_VERSION,     StrId::STR_DREAM_UPTIME,  StrId::STR_DREAM_INTERNAL_MEMORY,
                              StrId::STR_DREAM_PSRAM,       StrId::STR_DREAM_STORAGE, StrId::STR_DREAM_WIFI,
                              StrId::STR_DREAM_CRASH_REPORT};
  for (int i = 0; i < 8; ++i) {
    rows[i] = {};
    rows[i].label = i == 7 ? tr(STR_DREAM_REFRESH_HEALTH) : I18N.get(labels[i]);
    rows[i].subtitle = i < 7 ? values[i] : nullptr;
    rows[i].actionValue = static_cast<int16_t>(i);
  }
  requestUpdate();
}
void SystemHealthActivity::activateIndex(int index) {
  app.clearTapFlash();
  if (index == 7) sample();
}
void SystemHealthActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                                static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  uiTextBand(screen, tr(STR_DREAM_HEALTH_HELP), screen.theme().smallText, 3);
  fui::ListProps props;
  props.items = rows;
  props.count = 8;
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;
  props.labelText = screen.theme().bodyText;
  props.subtitleText = screen.theme().smallText;
  props.subtitleText.maxLines = 2;
  syncListViewport(screen, props);
  screen.list(props);
}
#endif

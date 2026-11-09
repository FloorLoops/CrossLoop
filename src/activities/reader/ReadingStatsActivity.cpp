#include "ReadingStatsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Memory.h>

#include "activities/util/ConfirmationActivity.h"
#include "components/UITheme.h"
#include "components/UiTextBand.h"
#include "util/LocalReadingStats.h"

namespace fui = freeink::ui;
void ReadingStatsActivity::onEnter() {
  UiListActivity::onEnter();
  reload();
}
const char* ReadingStatsActivity::headerTitle() const { return tr(STR_READING_STATS); }
void ReadingStatsActivity::reload() {
  crossloop::ReadingStatsData data;
  const bool loaded = crossloop::loadReadingStats(data);
  RenderLock lock(*this);
  failed = !loaded;
  count = 0;
  nav.reset();
  if (loaded) {
    snprintf(labels[count++], sizeof(labels[0]), tr(STR_STATS_TOTAL),
             static_cast<unsigned long long>(data.seconds / 3600), static_cast<unsigned>((data.seconds / 60) % 60));
    snprintf(labels[count++], sizeof(labels[0]), tr(STR_STATS_TURNS), static_cast<unsigned long>(data.turns));
    snprintf(labels[count++], sizeof(labels[0]), tr(STR_STATS_SESSIONS), static_cast<unsigned long>(data.sessions));
    for (const auto& day : data.days) {
      if (!day.date) break;
      snprintf(labels[count++], sizeof(labels[0]), tr(STR_STATS_DAY), static_cast<unsigned long>(day.date / 10000),
               static_cast<unsigned long>((day.date / 100) % 100), static_cast<unsigned long>(day.date % 100),
               static_cast<unsigned long>(day.seconds / 60));
    }
  }
  snprintf(labels[count++], sizeof(labels[0]), "%s", tr(STR_STATS_RESET));
  for (int i = 0; i < count; ++i) {
    rows[i] = {};
    rows[i].label = labels[i];
    rows[i].actionValue = static_cast<int16_t>(i);
  }
  requestUpdate();
}
void ReadingStatsActivity::activateIndex(int index) {
  app.clearTapFlash();
  if (index != count - 1) return;
  auto confirmation =
      makeUniqueNoThrow<ConfirmationActivity>(renderer, mappedInput, tr(STR_STATS_RESET), tr(STR_STATS_RESET_CONFIRM));
  if (!confirmation) return;
  startActivityForResult(std::move(confirmation), [this](const ActivityResult& result) {
    if (result.isCancelled) return;
    const bool saved = crossloop::resetReadingStats();
    reload();
    if (!saved) {
      RenderLock lock(*this);
      failed = true;
      requestUpdate();
    }
  });
}
void ReadingStatsActivity::buildScreen(UiScreen& view) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  view.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                              static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  uiTextBand(view, failed ? tr(STR_STATS_FAILED) : tr(STR_STATS_HELP), view.theme().smallText, 3);
  view.spacer(static_cast<int16_t>(metrics.verticalSpacing));
  fui::ListProps props;
  props.items = rows;
  props.count = count;
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;
  syncListViewport(view, props);
  view.list(props);
}

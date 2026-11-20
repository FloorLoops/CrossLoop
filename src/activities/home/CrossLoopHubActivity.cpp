#include "CrossLoopHubActivity.h"
#if CROSSLOOP_DREAM_OS
#include <HalClock.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Memory.h>

#include <ctime>

#include "FileBrowserActivity.h"
#include "RecentBooksStore.h"
#include "activities/library/LibraryListActivity.h"
#include "activities/library/SavedItemsActivity.h"
#include "activities/network/CalendarActivity.h"
#include "activities/network/FeedActivity.h"
#include "activities/network/ImageImportActivity.h"
#include "activities/network/NativeBrowserActivity.h"
#include "activities/network/NetworkModeSelectionActivity.h"
#include "activities/network/TextWebActivity.h"
#include "activities/network/TickTickActivity.h"
#include "activities/network/WeatherActivity.h"
#include "activities/reader/ReadingStatsActivity.h"
#include "activities/settings/ClockSettingsActivity.h"
#include "activities/settings/SettingsListActivity.h"
#include "activities/settings/SystemHealthActivity.h"
#include "components/UITheme.h"
#include "components/UiTextBand.h"
#include "util/LocalReadingStats.h"
#include "util/ReadingGoal.h"

namespace fui = freeink::ui;
namespace {
constexpr StrId TABS[] = {StrId::STR_DREAM_READ, StrId::STR_DREAM_TODAY, StrId::STR_DREAM_EXPLORE,
                          StrId::STR_DREAM_DEVICE};
constexpr StrId LABELS[4][6] = {
    {StrId::STR_RESUME, StrId::STR_LIBRARY, StrId::STR_DREAM_PINNED_BOOKS, StrId::STR_DREAM_READ_LATER,
     StrId::STR_DREAM_WORD_BANK, StrId::STR_READING_STATS},
    {StrId::STR_DREAM_DAILY_GOAL, StrId::STR_PERSONAL_SLEEP_TICKTICK, StrId::STR_CALENDAR, StrId::STR_DREAM_WEATHER},
    {StrId::STR_NATIVE_BROWSER, StrId::STR_DREAM_BOOKMARKS, StrId::STR_DREAM_FEEDS, StrId::STR_DREAM_IMAGE_IMPORT,
     StrId::STR_TEXT_WEB},
    {StrId::STR_SETTINGS_TITLE, StrId::STR_FILE_TRANSFER, StrId::STR_DREAM_CLOCK, StrId::STR_DREAM_HEALTH}};
constexpr int COUNTS[] = {6, 4, 5, 4};
}  // namespace
int CrossLoopHubActivity::listCount() const { return COUNTS[tab]; }
const char* CrossLoopHubActivity::headerTitle() const { return tr(STR_DREAM_APPS); }
const char* CrossLoopHubActivity::tabLabel(int index) const { return I18N.get(TABS[index]); }
void CrossLoopHubActivity::onEnter() {
  UiTabListActivity::onEnter();
  refreshOverview();
}
void CrossLoopHubActivity::onExit() {
  closeRouting();
  Activity::onExit();
}
void CrossLoopHubActivity::refreshOverview() {
  crossloop::ReadingStatsData stats;
  uint16_t goal;
  const auto date = crossloop::readingDate(time(nullptr));
  const bool valid = crossloop::loadReadingStats(stats) && crossloop::loadReadingGoal(goal);
  RenderLock lock(*this);
  if (!date)
    snprintf(overview, sizeof(overview), "%s", tr(STR_DREAM_CLOCK_NEEDED));
  else if (!valid)
    snprintf(overview, sizeof(overview), "%s", tr(STR_STATS_FAILED));
  else
    snprintf(overview, sizeof(overview), tr(STR_DREAM_GOAL_PROGRESS), static_cast<unsigned>(stats.secondsOn(date) / 60),
             static_cast<unsigned>(goal));
  requestUpdate();
}
void CrossLoopHubActivity::onTabAction(int index) {
  if (index < 0 || index >= tabCount()) return;
  RenderLock lock(*this);
  app.clearTapFlash();
  tab = index;
  activeNav().followOnBuild = true;
  requestUpdate();
}
void CrossLoopHubActivity::stepTab(int direction) { onTabAction((tab + direction + tabCount()) % tabCount()); }
bool CrossLoopHubActivity::handleButtons() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return true;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (ringPos() == 0)
      stepTab(1);
    else
      activateIndex(ringPos() - 1);
    return true;
  }
  return false;
}
void CrossLoopHubActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                                static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  uiTextBand(screen, overview, screen.theme().smallText, 2);
  screen.spacer(screen.theme().spaceSm);
  buildTabBar(screen);
  for (int i = 0; i < listCount(); ++i) {
    rows[i] = {};
    rows[i].label = I18N.get(LABELS[tab][i]);
    rows[i].actionValue = static_cast<int16_t>(i);
    if (tab == 0 && i == 0) {
      const auto& books = RECENT_BOOKS.getBooks();
      rows[i].enabled = !books.empty() && !RecentBooksStore::isMissing(books.front());
      if (!books.empty()) rows[i].subtitle = books.front().title.c_str();
    }
    if (tab == 1 && i == 0) rows[i].subtitle = overview;
    if (tab == 3 && i == 2) rows[i].enabled = halClock.isAvailable();
  }
  fui::ListProps props;
  props.items = rows;
  props.count = listCount();
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;
  props.labelText = screen.theme().bodyText;
  props.subtitleText = screen.theme().smallText;
  props.subtitleText.maxLines = 1;
  syncTabListViewport(screen, props);
  screen.list(props);
}
void CrossLoopHubActivity::openBrowser(const std::string& url) {
  auto activity = makeUniqueNoThrow<NativeBrowserActivity>(renderer, mappedInput, url);
  if (activity) startActivityForResult(std::move(activity), [this](const ActivityResult&) { refreshOverview(); });
}
void CrossLoopHubActivity::activateIndex(int index) {
  if (index < 0 || index >= listCount()) return;
  app.clearTapFlash();
  std::unique_ptr<Activity> activity;
  if (tab == 0) {
    switch (index) {
      case 0: {
        const auto& books = RECENT_BOOKS.getBooks();
        if (!books.empty() && !RecentBooksStore::isMissing(books.front())) onSelectBook(books.front().path);
        return;
      }
      case 1:
        activity = makeUniqueNoThrow<LibraryListActivity>(renderer, mappedInput);
        break;
      case 2:
        activity = makeUniqueNoThrow<SavedItemsActivity>(renderer, mappedInput, crossloop::saved::Kind::Book);
        break;
      case 3:
        if (Storage.ensureDirectoryExists("/WebPages"))
          activity = makeUniqueNoThrow<FileBrowserActivity>(renderer, mappedInput, "/WebPages");
        else {
          RenderLock lock(*this);
          GUI.drawPopup(renderer, tr(STR_FILE_OPEN_FAILED));
        }
        break;
      case 4:
        activity = makeUniqueNoThrow<FileBrowserActivity>(renderer, mappedInput, "/Word Bank");
        break;
      case 5:
        activity = makeUniqueNoThrow<ReadingStatsActivity>(renderer, mappedInput);
        break;
    }
  } else if (tab == 1) {
    switch (index) {
      case 0:
        activity = makeUniqueNoThrow<ReadingStatsActivity>(renderer, mappedInput);
        break;
      case 1:
        activity = makeUniqueNoThrow<TickTickActivity>(renderer, mappedInput);
        break;
      case 2:
        activity = makeUniqueNoThrow<CalendarActivity>(renderer, mappedInput);
        break;
      case 3:
        activity = makeUniqueNoThrow<WeatherActivity>(renderer, mappedInput);
        break;
    }
  } else if (tab == 2) {
    switch (index) {
      case 0:
        openBrowser();
        return;
      case 1: {
        auto bookmarks = makeUniqueNoThrow<SavedItemsActivity>(renderer, mappedInput, crossloop::saved::Kind::Web);
        if (bookmarks)
          startActivityForResult(std::move(bookmarks), [this](const ActivityResult& result) {
            if (!result.isCancelled)
              if (const auto* address = std::get_if<WebAddressResult>(&result.data)) openBrowser(address->url);
          });
        return;
      }
      case 2:
        activity = makeUniqueNoThrow<FeedActivity>(renderer, mappedInput);
        break;
      case 3:
        activity = makeUniqueNoThrow<ImageImportActivity>(renderer, mappedInput);
        break;
      case 4:
        activity = makeUniqueNoThrow<TextWebActivity>(renderer, mappedInput);
        break;
    }
  } else {
    switch (index) {
      case 0:
        activity = makeUniqueNoThrow<SettingsListActivity>(renderer, mappedInput);
        break;
      case 1:
        activity = makeUniqueNoThrow<NetworkModeSelectionActivity>(renderer, mappedInput);
        break;
      case 2:
        if (halClock.isAvailable()) activity = makeUniqueNoThrow<ClockSettingsActivity>(renderer, mappedInput);
        break;
      case 3:
        activity = makeUniqueNoThrow<SystemHealthActivity>(renderer, mappedInput);
        break;
    }
  }
  if (activity) startActivityForResult(std::move(activity), [this](const ActivityResult&) { refreshOverview(); });
}
#endif

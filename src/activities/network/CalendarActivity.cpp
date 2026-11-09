#include "CalendarActivity.h"

#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <Memory.h>
#include <WiFi.h>

#include <algorithm>
#include <cstring>
#include <ctime>

#include "CrossPointSettings.h"
#include "WifiSelectionActivity.h"
#include "activities/util/ConfirmationActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "components/UiTextBand.h"
#include "util/CalendarDrawing.h"
#include "util/CalendarImport.h"
#include "util/CrossLoopPreferences.h"
#include "util/Timezones.h"

namespace fui = freeink::ui;
namespace cal = crossloop::calendar;
namespace data = crossloop::personal;
namespace prefs = crossloop::preferences;
namespace {
constexpr char RAW_PATH[] = "/.crosspoint/personal/calendar.part";
constexpr unsigned PAGE_ITEMS = 10;
}  // namespace
void CalendarActivity::onEnter() {
  UiListActivity::onEnter();
  menu();
}
void CalendarActivity::onExit() {
  closeRouting();
  download.stop();
  agenda.close();
  Storage.remove(RAW_PATH);
  if (ownsWifi) {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
  }
  Activity::onExit();
}
const char* CalendarActivity::headerTitle() const { return tr(STR_CALENDAR); }
void CalendarActivity::row(unsigned index, const char* label) {
  snprintf(labels[index], sizeof(labels[index]), "%s", label);
  rows[index] = {};
  rows[index].label = labels[index];
  rows[index].actionValue = static_cast<int16_t>(index);
}
void CalendarActivity::menu(StrId message) {
  RenderLock lock(*this);
  closeRouting();
  agenda.close();
  screen = Screen::Menu;
  status = message;
  nav.reset();
  timestamp[0] = 0;
  count = 5;
  row(0, tr(STR_CALENDAR_REFRESH));
  row(1, tr(STR_CALENDAR_AGENDA));
  row(2, tr(STR_CALENDAR_CONNECT));
  row(3, tr(STR_CALENDAR_SLEEP));
  row(4, tr(STR_TICK_DISCONNECT));
  requestUpdate();
}
bool CalendarActivity::clearCaches() {
  agenda.close();
  const bool full = data::removeCache(data::Dataset::Calendar);
  const bool preview = data::removeCache(data::Dataset::CalendarPreview);
  return full && preview;
}
void CalendarActivity::edit() {
  auto keyboard = makeUniqueNoThrow<KeyboardEntryActivity>(
      renderer, mappedInput, tr(STR_CALENDAR_URL), "", prefs::maxLength(prefs::Key::CalendarUrl), InputType::Password);
  if (!keyboard) {
    menu(StrId::STR_MEMORY_ERROR);
    return;
  }
  startActivityForResult(std::move(keyboard), [this](const ActivityResult& result) {
    if (result.isCancelled) return;
    const auto* entered = std::get_if<KeyboardResult>(&result.data);
    if (!entered || entered->text.empty() || !prefs::validValue(prefs::Key::CalendarUrl, entered->text)) {
      menu(StrId::STR_CALENDAR_BAD_URL);
      return;
    }
    menu();
    if (!prefs::write(prefs::Key::CalendarUrl, entered->text)) {
      menu(StrId::STR_TICK_SETTINGS_FAILED);
      return;
    }
    url.clear();
    menu(clearCaches() ? StrId::STR_CALENDAR_CONNECTED : StrId::STR_CALENDAR_CLEAR_FAILED);
  });
}
void CalendarActivity::disconnect() {
  auto confirmation =
      makeUniqueNoThrow<ConfirmationActivity>(renderer, mappedInput, tr(STR_TICK_DISCONNECT), tr(STR_CALENDAR_FORGET));
  if (!confirmation) {
    menu(StrId::STR_MEMORY_ERROR);
    return;
  }
  startActivityForResult(std::move(confirmation), [this](const ActivityResult& result) {
    if (result.isCancelled) return;
    menu();
    const bool forgotten = prefs::erase(prefs::Key::CalendarUrl);
    const bool cleared = clearCaches();
    url.clear();
    menu(forgotten && cleared ? StrId::STR_CALENDAR_DISCONNECTED : StrId::STR_CALENDAR_CLEAR_FAILED);
  });
}
void CalendarActivity::list(unsigned first) {
  RenderLock lock(*this);
  closeRouting();
  screen = Screen::Agenda;
  count = 0;
  nav.reset();
  timestamp[0] = 0;
  if (!agenda.open()) {
    status = StrId::STR_CALENDAR_NO_CACHE;
    requestUpdate();
    return;
  }
  status = agenda.windowEnd() <= static_cast<int64_t>(time(nullptr)) ? StrId::STR_CALENDAR_EXPIRED
                                                                     : StrId::STR_CALENDAR_CACHED;
  char synced[32]{};
  data::formatSyncTime(agenda.timestamp(), synced, sizeof(synced));
  snprintf(timestamp, sizeof(timestamp), tr(STR_PERSONAL_SNAPSHOT_TIME), synced[0] ? synced : "?");
  pageStart = first < agenda.size() ? first : 0;
  visibleItems = std::min(PAGE_ITEMS, agenda.size() - pageStart);
  row(0, tr(STR_PREV_PAGE));
  row(1, tr(STR_NEXT_PAGE));
  rows[0].enabled = pageStart > 0;
  rows[1].enabled = pageStart + visibleItems < agenda.size();
  count = 2;
  for (unsigned i = 0; i < visibleItems; ++i) {
    if (!agenda.read(pageStart + i, item)) {
      status = StrId::STR_FILE_OPEN_FAILED;
      break;
    }
    row(count, item.title[0] ? item.title : tr(STR_CALENDAR_UNTITLED));
    cal::formatEventWhen(item, dates[i], sizeof(dates[i]));
    rows[count++].subtitle = dates[i];
  }
  if (!agenda.size() && status != StrId::STR_CALENDAR_EXPIRED) status = StrId::STR_CALENDAR_EMPTY;
  requestUpdate();
}
void CalendarActivity::detail(unsigned index) {
  RenderLock lock(*this);
  closeRouting();
  if (!agenda.read(index, item)) {
    status = StrId::STR_FILE_OPEN_FAILED;
    requestUpdate();
    return;
  }
  screen = Screen::Detail;
  status = StrId::STR_CALENDAR_READ_ONLY;
  nav.reset();
  count = 0;
  row(count++, item.title[0] ? item.title : tr(STR_CALENDAR_UNTITLED));
  cal::formatEventWhen(item, dates[0], sizeof(dates[0]));
  row(count++, dates[0]);
  if (item.detail[0]) row(count++, item.detail);
  // Short, independently wrapped rows keep the entire saved description
  // scrollable on both portrait and landscape panels.
  size_t pos = 0, length = strlen(item.target);
  while (pos < length && count < 15) {
    size_t end = std::min(pos + 128, length);
    while (end > pos && (static_cast<unsigned char>(item.target[end]) & 0xc0) == 0x80) --end;
    row(count, "");
    memcpy(labels[count], item.target + pos, end - pos);
    labels[count++][end - pos] = 0;
    pos = end;
    while (item.target[pos] == ' ') ++pos;
  }
  // Detail rows are informational; Confirm returns to the same agenda page.
  row(count++, tr(STR_CALENDAR_AGENDA));
  requestUpdate();
}
void CalendarActivity::connect() {
  if (!prefs::read(prefs::Key::CalendarUrl, url)) {
    menu(StrId::STR_TICK_SETTINGS_FAILED);
    return;
  }
  if (url.empty()) {
    edit();
    return;
  }
  if (WiFi.status() == WL_CONNECTED) {
    startDownload();
    return;
  }
  ownsWifi = ownsWifi || WiFi.getMode() == WIFI_MODE_NULL;
  auto wifi = makeUniqueNoThrow<WifiSelectionActivity>(renderer, mappedInput);
  if (!wifi) {
    menu(StrId::STR_MEMORY_ERROR);
    return;
  }
  startActivityForResult(std::move(wifi), [this](const ActivityResult& result) {
    const auto* connected = std::get_if<WifiResult>(&result.data);
    if (!result.isCancelled && connected && connected->connected)
      startDownload();
    else
      menu(StrId::STR_WIFI_CONN_FAILED);
  });
}
void CalendarActivity::startDownload() {
  if (!Storage.ensureDirectoryExists("/.crosspoint/personal")) {
    menu(StrId::STR_FILE_OPEN_FAILED);
    return;
  }
  {
    RenderLock lock(*this);
    closeRouting();
    agenda.close();
    screen = Screen::Downloading;
    status = StrId::STR_CALENDAR_LOADING;
    nav.reset();
    timestamp[0] = 0;
    count = 1;
    row(0, tr(STR_CANCEL));
  }
  requestUpdateAndWait();
  {
    RenderLock lock(*this);
    if (auto* fonts = renderer.getFontCacheManager()) fonts->releaseSdFontCaches();
  }
  if (!download.start(url, RAW_PATH, 2 * 1024 * 1024)) menu(StrId::STR_MEMORY_ERROR);
}
void CalendarActivity::done() {
  const auto result = download.result();
  download.stop();
  if (result != VerifiedDownload::Result::Ok) {
    Storage.remove(RAW_PATH);
    menu(result == VerifiedDownload::Result::Memory ? StrId::STR_MEMORY_ERROR : StrId::STR_CALENDAR_FAILED);
    return;
  }
  {
    RenderLock lock(*this);
    closeRouting();
    screen = Screen::Preparing;
    status = StrId::STR_CALENDAR_PREPARING;
    count = 0;
  }
  requestUpdateAndWait();
  const time_t now = time(nullptr);
  tm civil{};
  cal::ImportResult imported = cal::ImportResult::Clock;
  if (now >= 1735689600 && now < 4102444800LL && localtime_r(&now, &civil)) {
    civil.tm_hour = civil.tm_min = civil.tm_sec = 0;
    civil.tm_isdst = -1;
    const time_t from = mktime(&civil);
    civil.tm_mday += 30;
    civil.tm_isdst = -1;
    const time_t through = mktime(&civil);
    imported = cal::importCalendar(RAW_PATH, static_cast<int64_t>(from), static_cast<int64_t>(through));
  }
  Storage.remove(RAW_PATH);
  if (imported != cal::ImportResult::Ok) {
    switch (imported) {
      case cal::ImportResult::Memory:
        menu(StrId::STR_MEMORY_ERROR);
        break;
      case cal::ImportResult::Clock:
        menu(StrId::STR_CALENDAR_CLOCK);
        break;
      case cal::ImportResult::Unsupported:
        menu(StrId::STR_CALENDAR_UNSUPPORTED);
        break;
      case cal::ImportResult::TooLarge:
        menu(StrId::STR_CALENDAR_TOO_LARGE);
        break;
      default:
        menu(StrId::STR_CALENDAR_INVALID);
        break;
    }
    return;
  }
  const bool preview = cal::prepareCalendarPreview(static_cast<int64_t>(now));
  list();
  if (!preview) {
    RenderLock lock(*this);
    status = StrId::STR_CALENDAR_PREVIEW_FAILED;
    requestUpdate();
  }
}
void CalendarActivity::activateIndex(int index) {
  app.clearTapFlash();
  if (screen == Screen::Downloading) {
    download.cancel();
    return;
  }
  if (screen == Screen::Preparing) return;
  if (screen == Screen::Detail) {
    list(pageStart);
    return;
  }
  if (screen == Screen::Agenda) {
    if (index == 0 && pageStart)
      list(pageStart - PAGE_ITEMS);
    else if (index == 1 && pageStart + visibleItems < agenda.size())
      list(pageStart + PAGE_ITEMS);
    else if (index >= 2 && index < count)
      detail(pageStart + index - 2);
    return;
  }
  if (index == 0)
    connect();
  else if (index == 1)
    list();
  else if (index == 2)
    edit();
  else if (index == 4)
    disconnect();
  else if (index == 3) {
    const auto previous = SETTINGS.sleepScreen;
    SETTINGS.sleepScreen = CrossPointSettings::PERSONAL_TODAY;
    const bool saved = SETTINGS.saveToFile();
    if (!saved) SETTINGS.sleepScreen = previous;
    menu(saved ? StrId::STR_DONE : StrId::STR_TICK_SETTINGS_FAILED);
  }
}
void CalendarActivity::onBackButton() {
  if (screen == Screen::Downloading)
    download.cancel();
  else if (screen == Screen::Preparing)
    return;
  else if (screen == Screen::Detail)
    list(pageStart);
  else if (screen != Screen::Menu)
    menu();
  else
    finish();
}
void CalendarActivity::loop() {
  if (screen == Screen::Downloading && download.done()) {
    done();
    return;
  }
  if (screen != Screen::Preparing) UiListActivity::loop();
}
void CalendarActivity::buildScreen(UiScreen& view) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  view.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                              static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  uiTextBand(view, I18N.get(status), view.theme().smallText, screen == Screen::Menu ? 3 : 2);
  if (timestamp[0]) uiTextBand(view, timestamp, view.theme().smallText);
  if (screen == Screen::Agenda || screen == Screen::Detail)
    uiTextBand(view, timezones::table()[timezones::activeIndex()].name, view.theme().smallText);
  fui::ListProps props;
  props.items = rows;
  props.count = count;
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;
  props.labelText = view.theme().bodyText;
  props.labelText.maxLines = screen == Screen::Detail ? 5 : 3;
  props.subtitleText = view.theme().smallText;
  props.subtitleText.maxLines = 2;
  syncListViewport(view, props);
  view.list(props);
}

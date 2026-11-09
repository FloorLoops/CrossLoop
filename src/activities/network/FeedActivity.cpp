#include "FeedActivity.h"

#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <Memory.h>
#include <WiFi.h>

#include <algorithm>

#include "TextWebActivity.h"
#include "WifiSelectionActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "components/UiTextBand.h"
#include "network/WebUrl.h"
#include "util/CrossLoopPreferences.h"
#include "util/FeedParser.h"

namespace fui = freeink::ui;
namespace data = crossloop::personal;
namespace prefs = crossloop::preferences;
namespace {
constexpr char RAW_PATH[] = "/.crosspoint/personal/feed.part";
constexpr unsigned PAGE_ITEMS = 10;
}  // namespace
bool FeedActivity::loadFeeds() {
  std::string configured;
  if (!prefs::read(prefs::Key::FeedUrls, configured)) return false;
  feeds = {};
  size_t begin = 0;
  unsigned index = 0;
  while (begin < configured.size() && index < feeds.size()) {
    const auto end = configured.find('\n', begin);
    feeds[index++] = configured.substr(begin, end == std::string::npos ? end : end - begin);
    if (end == std::string::npos) break;
    begin = end + 1;
  }
  return true;
}
void FeedActivity::onEnter() {
  UiListActivity::onEnter();
  if (!loadFeeds()) status = StrId::STR_TICK_SETTINGS_FAILED;
  menu();
}
void FeedActivity::onExit() {
  closeRouting();
  download.stop();
  writer.cancel();
  cache.close();
  Storage.remove(RAW_PATH);
  if (ownsWifi) {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
  }
  Activity::onExit();
}
const char* FeedActivity::headerTitle() const { return tr(STR_FEEDS); }
void FeedActivity::row(int index, const char* label) {
  snprintf(labels[index], sizeof(labels[index]), "%s", label);
  rows[index] = {};
  rows[index].label = labels[index];
  rows[index].actionValue = static_cast<int16_t>(index);
}
void FeedActivity::menu() {
  RenderLock lock(*this);
  cache.close();
  screen = Screen::Home;
  closeRouting();
  nav.reset();
  timestamp[0] = 0;
  count = 6;
  row(0, tr(STR_FEED_REFRESH));
  row(1, tr(STR_FEED_ARTICLES));
  for (unsigned i = 0; i < feeds.size(); ++i) {
    row(i + 2, "");
    snprintf(labels[i + 2], sizeof(labels[0]), "%u. %s", i + 1, feeds[i].empty() ? tr(STR_FEED_ADD) : feeds[i].c_str());
  }
  requestUpdate();
}
void FeedActivity::fail(StrId reason) {
  writer.cancel();
  Storage.remove(RAW_PATH);
  menu();
  RenderLock lock(*this);
  status = reason;
  requestUpdate();
}
void FeedActivity::articles(unsigned first) {
  RenderLock lock(*this);
  screen = Screen::Articles;
  closeRouting();
  nav.reset();
  count = 0;
  timestamp[0] = 0;
  if (!cache.open(data::Dataset::Feeds)) {
    status = StrId::STR_FEED_NO_CACHE;
    requestUpdate();
    return;
  }
  pageStart = first < cache.size() ? first : 0;
  visibleItems = std::min(PAGE_ITEMS, cache.size() - pageStart);
  data::formatSyncTime(cache.timestamp(), timestamp, sizeof(timestamp));
  status = StrId::STR_FEED_CACHED;
  row(0, tr(STR_PREV_PAGE));
  row(1, tr(STR_NEXT_PAGE));
  rows[0].enabled = pageStart > 0;
  rows[1].enabled = pageStart + visibleItems < cache.size();
  count = 2;
  for (unsigned i = 0; i < visibleItems; ++i) {
    if (!cache.read(pageStart + i, item)) {
      status = StrId::STR_FILE_OPEN_FAILED;
      break;
    }
    row(count++, item.title);
  }
  if (!cache.size()) status = StrId::STR_FEED_EMPTY;
  requestUpdate();
}
void FeedActivity::editFeed(unsigned index) {
  auto keyboard = makeUniqueNoThrow<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_FEED_EDIT), feeds[index],
                                                           crossloop::web::MAX_URL, InputType::Url);
  if (!keyboard) {
    fail(StrId::STR_MEMORY_ERROR);
    return;
  }
  startActivityForResult(std::move(keyboard), [this, index](const ActivityResult& result) {
    if (result.isCancelled) return;
    const auto* entered = std::get_if<KeyboardResult>(&result.data);
    if (!entered || (!entered->text.empty() && !crossloop::web::validUrl(entered->text))) {
      fail(StrId::STR_WEB_BAD_URL);
      return;
    }
    std::string joined;
    for (unsigned i = 0; i < feeds.size(); ++i) {
      const auto& value = i == index ? entered->text : feeds[i];
      if (value.empty()) continue;
      if (!joined.empty()) joined += '\n';
      joined += value;
    }
    if (!prefs::write(prefs::Key::FeedUrls, joined) || !loadFeeds()) {
      fail(StrId::STR_TICK_SETTINGS_FAILED);
      return;
    }
    menu();
  });
}
void FeedActivity::connect() {
  if (feeds[0].empty()) {
    fail(StrId::STR_FEED_HELP);
    return;
  }
  if (WiFi.status() == WL_CONNECTED) {
    beginSync();
    return;
  }
  ownsWifi = ownsWifi || WiFi.getMode() == WIFI_MODE_NULL;
  auto wifi = makeUniqueNoThrow<WifiSelectionActivity>(renderer, mappedInput);
  if (!wifi) {
    fail(StrId::STR_MEMORY_ERROR);
    return;
  }
  startActivityForResult(std::move(wifi), [this](const ActivityResult& result) {
    const auto* connected = std::get_if<WifiResult>(&result.data);
    if (!result.isCancelled && connected && connected->connected)
      beginSync();
    else
      fail(StrId::STR_WIFI_CONN_FAILED);
  });
}
void FeedActivity::beginSync() {
  cache.close();
  if (!writer.begin(data::Dataset::Feeds)) {
    fail(StrId::STR_FILE_OPEN_FAILED);
    return;
  }
  nextFeed = 0;
  if (!fetchNext()) fail(StrId::STR_MEMORY_ERROR);
}
bool FeedActivity::fetchNext() {
  if (nextFeed >= feeds.size() || feeds[nextFeed].empty()) return false;
  {
    RenderLock lock(*this);
    screen = Screen::Busy;
    closeRouting();
    count = 1;
    nav.reset();
    row(0, tr(STR_CANCEL));
    status = StrId::STR_FEED_LOADING;
    timestamp[0] = 0;
  }
  requestUpdateAndWait();
  {
    RenderLock lock(*this);
    if (auto* fonts = renderer.getFontCacheManager()) fonts->releaseSdFontCaches();
  }
  return download.start(feeds[nextFeed++], RAW_PATH, 512 * 1024);
}
void FeedActivity::done() {
  const auto result = download.result();
  download.stop();
  if (result != VerifiedDownload::Result::Ok) {
    fail(result == VerifiedDownload::Result::Memory ? StrId::STR_MEMORY_ERROR : StrId::STR_FEED_FAILED);
    return;
  }
  const bool parsed = data::parseFeed(RAW_PATH, download.finalUrl(), writer);
  Storage.remove(RAW_PATH);
  if (!parsed) {
    fail(StrId::STR_FEED_INVALID);
    return;
  }
  if (nextFeed < feeds.size() && !feeds[nextFeed].empty()) {
    if (!fetchNext()) fail(StrId::STR_MEMORY_ERROR);
    return;
  }
  const bool saved = writer.commit();
  writer.cancel();
  if (!saved) {
    fail(StrId::STR_FEED_FAILED);
    return;
  }
  articles();
}
void FeedActivity::activateIndex(int index) {
  app.clearTapFlash();
  if (screen == Screen::Busy) {
    download.cancel();
    return;
  }
  if (screen == Screen::Home) {
    if (index == 0)
      connect();
    else if (index == 1)
      articles();
    else if (index >= 2 && index < 6)
      editFeed(index - 2);
    return;
  }
  if (index == 0 && pageStart)
    articles(pageStart - PAGE_ITEMS);
  else if (index == 1 && pageStart + visibleItems < cache.size())
    articles(pageStart + PAGE_ITEMS);
  else if (index >= 2 && index < count) {
    if (!cache.read(pageStart + index - 2, item)) {
      fail(StrId::STR_FILE_OPEN_FAILED);
      return;
    }
    auto web = makeUniqueNoThrow<TextWebActivity>(renderer, mappedInput, std::string(item.target));
    if (!web) {
      fail(StrId::STR_MEMORY_ERROR);
      return;
    }
    startActivityForResult(std::move(web), [this](const ActivityResult&) { articles(pageStart); });
  }
}
void FeedActivity::onBackButton() {
  if (screen == Screen::Busy)
    download.cancel();
  else if (screen != Screen::Home)
    menu();
  else
    finish();
}
void FeedActivity::loop() {
  if (screen == Screen::Busy && download.done()) {
    done();
    return;
  }
  UiListActivity::loop();
}
void FeedActivity::buildScreen(UiScreen& view) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  view.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                              static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  uiTextBand(view, I18N.get(status), view.theme().smallText, 2);
  if (timestamp[0]) uiTextBand(view, timestamp, view.theme().smallText);
  view.spacer(static_cast<int16_t>(metrics.verticalSpacing));
  fui::ListProps props;
  props.items = rows;
  props.count = count;
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;
  syncListViewport(view, props);
  view.list(props);
}

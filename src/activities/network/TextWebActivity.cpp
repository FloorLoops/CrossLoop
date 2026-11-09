#include "TextWebActivity.h"

#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <Memory.h>
#include <WiFi.h>
#include <strings.h>

#include <cstring>

#include "FeedActivity.h"
#include "WebReaderActivity.h"
#include "activities/home/FileBrowserActivity.h"
#include "activities/network/WifiSelectionActivity.h"
#include "activities/util/ConfirmationActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "components/UiTextBand.h"
#include "network/WebUrl.h"
#include "util/PersonalCache.h"

namespace fui = freeink::ui;
namespace {
constexpr char RAW_PATH[] = "/.crosspoint/web/download.part";
}

void TextWebActivity::onEnter() {
  UiListActivity::onEnter();
  showHome();
  if (!requestedUrl.empty()) {
    const auto key = crossloop::web::pageKey(requestedUrl);
    if (crossloop::web::readPageInfo(key, page))
      showPage(key);
    else
      fetchUrl(requestedUrl);
  }
}
void TextWebActivity::onExit() {
  closeRouting();
  download.stop();
  Storage.remove(RAW_PATH);
  if (ownsWifi) {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
  }
  Activity::onExit();
}
const char* TextWebActivity::headerTitle() const { return tr(STR_TEXT_WEB); }
void TextWebActivity::setRow(int index, const char* label) {
  snprintf(labels[index], sizeof(labels[index]), "%s", label);
  rows[index] = {};
  rows[index].label = labels[index];
  rows[index].actionValue = static_cast<int16_t>(index);
}
void TextWebActivity::showHome() {
  RenderLock lock(*this);
  screen = Screen::Home;
  closeRouting();
  previousKey.clear();
  referrerKey.clear();
  count = feedsEnabled ? 4 : 3;
  setRow(0, tr(STR_WEB_OPEN_URL));
  setRow(1, tr(STR_WEB_SAVED));
  setRow(2, tr(STR_PERSONAL_READ_LATER));
  setRow(3, tr(STR_FEEDS));
  nav.reset();
  requestUpdate();
}
void TextWebActivity::showSaved() {
  RenderLock lock(*this);
  screen = Screen::Saved;
  closeRouting();
  count = 0;
  nav.reset();
  auto directory = Storage.open(crossloop::web::CACHE_DIRECTORY);
  for (auto file = directory.openNextFile(); file && count < 32; file = directory.openNextFile()) {
    char name[48]{};
    file.getName(name, sizeof(name));
    const bool primary = strlen(name) == 36 && strcmp(name + 32, ".web") == 0;
    const bool backup = strlen(name) == 40 && strcmp(name + 32, ".web.bak") == 0;
    if (!primary && !backup) continue;
    name[32] = 0;
    bool duplicate = false;
    for (int i = 0; i < count; ++i)
      if (!strcmp(keys[i], name)) duplicate = true;
    if (duplicate) continue;
    if (!crossloop::web::readPageInfo(name, page)) continue;
    snprintf(keys[count], sizeof(keys[count]), "%s", name);
    setRow(count++, page.url);
  }
  status = count ? StrId::STR_WEB_OFFLINE : StrId::STR_WEB_NO_SAVED;
  requestUpdate();
}
void TextWebActivity::showPage(const std::string& key) {
  RenderLock lock(*this);
  if (!crossloop::web::readPageInfo(key, page)) {
    status = StrId::STR_FILE_OPEN_FAILED;
    requestUpdate();
    return;
  }
  currentKey = key;
  screen = Screen::Page;
  closeRouting();
  nav.reset();
  count = 6 + page.links;
  setRow(0, tr(STR_WEB_READ_PAGE));
  setRow(1, tr(STR_WEB_RELOAD));
  setRow(2, tr(STR_WEB_OPEN_URL));
  setRow(3, tr(STR_WEB_SAVED));
  setRow(4, tr(STR_WEB_DELETE_PAGE));
  setRow(5, tr(STR_WEB_EXPORT));
  for (unsigned i = 0; i < page.links; ++i) {
    if (crossloop::web::readPageLink(key, i, link))
      setRow(6 + i, link.label);
    else
      setRow(6 + i, tr(STR_WEB_UNSUPPORTED_LINK));
  }
  status = StrId::STR_WEB_OFFLINE;
  requestUpdate();
}
void TextWebActivity::fail(StrId message) {
  showHome();
  RenderLock lock(*this);
  status = message;
  requestUpdate();
}
void TextWebActivity::enterUrl() {
  auto keyboard = makeUniqueNoThrow<KeyboardEntryActivity>(
      renderer, mappedInput, tr(STR_WEB_OPEN_URL), std::string("https://"), crossloop::web::MAX_URL, InputType::Url);
  if (!keyboard) {
    fail(StrId::STR_MEMORY_ERROR);
    return;
  }
  startActivityForResult(std::move(keyboard), [this](const ActivityResult& result) {
    if (result.isCancelled) return;
    if (const auto* value = std::get_if<KeyboardResult>(&result.data)) fetchUrl(value->text);
  });
}
void TextWebActivity::fetchUrl(std::string url) {
  if (url.find("://") == std::string::npos) url.insert(0, "https://");
  if (!crossloop::web::validUrl(url)) {
    fail(StrId::STR_WEB_BAD_URL);
    return;
  }
  requestedUrl = std::move(url);
  if (WiFi.status() == WL_CONNECTED) {
    startDownload();
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
      startDownload();
    else
      fail(StrId::STR_WIFI_CONN_FAILED);
  });
}
void TextWebActivity::startDownload() {
  if (!Storage.ensureDirectoryExists(crossloop::web::CACHE_DIRECTORY)) {
    fail(StrId::STR_FILE_OPEN_FAILED);
    return;
  }
  // 32 persisted pages bound storage/index traversal without evicting a page
  // the user explicitly kept. Existing URLs remain refreshable at the limit.
  unsigned pages = 0;
  auto directory = Storage.open(crossloop::web::CACHE_DIRECTORY);
  for (auto file = directory.openNextFile(); file; file = directory.openNextFile()) {
    char name[48]{};
    file.getName(name, sizeof(name));
    const bool primary = strlen(name) == 36 && !strcmp(name + 32, ".web");
    const bool backup = strlen(name) == 40 && !strcmp(name + 32, ".web.bak");
    if (!primary && !backup) continue;
    name[32] = 0;
    bool duplicate = false;
    for (unsigned i = 0; i < pages; ++i)
      if (!strcmp(keys[i], name)) duplicate = true;
    if (duplicate) continue;
    snprintf(keys[pages++], sizeof(keys[0]), "%s", name);
    if (pages >= 32) break;
  }
  cacheWasFull = pages >= 32;
  if (cacheWasFull && !crossloop::web::readPageInfo(crossloop::web::pageKey(requestedUrl), page)) {
    fail(StrId::STR_WEB_CACHE_FULL);
    return;
  }
  {
    RenderLock lock(*this);
    screen = Screen::Downloading;
    closeRouting();
    count = 1;
    nav.reset();
    setRow(0, tr(STR_CANCEL));
    status = StrId::STR_LOADING;
  }
  requestUpdateAndWait();
  {
    RenderLock lock(*this);
    if (auto* cache = renderer.getFontCacheManager()) cache->releaseSdFontCaches();
  }
  if (!download.start(requestedUrl, RAW_PATH, 512 * 1024)) fail(StrId::STR_MEMORY_ERROR);
}
void TextWebActivity::openText() {
  auto reader = makeUniqueNoThrow<WebReaderActivity>(renderer, mappedInput, currentKey);
  if (!reader) {
    fail(StrId::STR_MEMORY_ERROR);
    return;
  }
  startActivityForResult(std::move(reader), [this](const ActivityResult&) { showPage(currentKey); });
}
void TextWebActivity::removePage() {
  auto confirm = makeUniqueNoThrow<ConfirmationActivity>(renderer, mappedInput, tr(STR_WEB_DELETE_PAGE), page.url);
  if (!confirm) {
    fail(StrId::STR_MEMORY_ERROR);
    return;
  }
  startActivityForResult(std::move(confirm), [this](const ActivityResult& result) {
    if (result.isCancelled) return;
    const std::string path = crossloop::web::pagePath(currentKey);
    if ((Storage.exists(path.c_str()) && !Storage.remove(path.c_str())) ||
        (Storage.exists((path + ".bak").c_str()) && !Storage.remove((path + ".bak").c_str()))) {
      fail(StrId::STR_FILE_OPEN_FAILED);
      return;
    }
    showSaved();
  });
}
void TextWebActivity::activateIndex(int index) {
  app.clearTapFlash();
  switch (screen) {
    case Screen::Home:
      if (index == 0)
        enterUrl();
      else if (index == 1)
        showSaved();
      else if (index == 2) {
        if (!Storage.ensureDirectoryExists("/WebPages")) {
          fail(StrId::STR_FILE_OPEN_FAILED);
          return;
        }
        activityManager.goToFileBrowser("/WebPages");
      } else if (index == 3 && feedsEnabled) {
        auto feeds = makeUniqueNoThrow<FeedActivity>(renderer, mappedInput);
        if (!feeds) {
          fail(StrId::STR_MEMORY_ERROR);
          return;
        }
        startActivityForResult(std::move(feeds), [this](const ActivityResult&) { showHome(); });
      }
      return;
    case Screen::Saved:
      if (index >= 0 && index < count) showPage(keys[index]);
      return;
    case Screen::Page:
      if (index == 0)
        openText();
      else if (index == 1) {
        referrerKey = previousKey;
        fetchUrl(page.url);
      } else if (index == 2)
        enterUrl();
      else if (index == 3)
        showSaved();
      else if (index == 4)
        removePage();
      else if (index == 5) {
        std::string path;
        const bool saved = crossloop::web::exportPageText(currentKey, path);
        RenderLock lock(*this);
        status = saved ? StrId::STR_WEB_EXPORTED : StrId::STR_WEB_SAVE_FAILED;
        requestUpdate();
      } else if (index >= 6 && crossloop::web::readPageLink(currentKey, index - 6, link)) {
        referrerKey = currentKey;
        fetchUrl(link.url);
      }
      return;
    case Screen::Downloading:
      download.cancel();
      return;
  }
}
void TextWebActivity::onBackButton() {
  if (screen == Screen::Downloading)
    download.cancel();
  else if (screen == Screen::Page && !previousKey.empty()) {
    const std::string back = std::move(previousKey);
    previousKey.clear();
    showPage(back);
  } else if (screen != Screen::Home)
    showHome();
  else
    finish();
}
void TextWebActivity::loop() {
  if (screen == Screen::Downloading && download.done()) {
    const auto result = download.result();
    download.stop();
    if (result == VerifiedDownload::Result::Ok) {
      const char* type = download.contentType();
      const bool plain = crossloop::web::mediaTypeIs(type, "text/plain");
      const bool html =
          crossloop::web::mediaTypeIs(type, "text/html") || crossloop::web::mediaTypeIs(type, "application/xhtml+xml");
      std::string key;
      if (cacheWasFull && !crossloop::web::readPageInfo(crossloop::web::pageKey(download.finalUrl()), page))
        fail(StrId::STR_WEB_CACHE_FULL);
      else if (!plain && !html)
        fail(StrId::STR_WEB_UNSUPPORTED_CONTENT);
      else if (crossloop::web::importPage(RAW_PATH, download.finalUrl(), plain, key)) {
        previousKey = std::move(referrerKey);
        referrerKey.clear();
        showPage(key);
      } else
        fail(StrId::STR_WEB_SAVE_FAILED);
    } else if (result == VerifiedDownload::Result::Cancelled)
      showHome();
    else if (result == VerifiedDownload::Result::Clock)
      fail(StrId::STR_WEB_CLOCK);
    else if (result == VerifiedDownload::Result::TooLarge)
      fail(StrId::STR_WEB_TOO_LARGE);
    else if (result == VerifiedDownload::Result::Memory)
      fail(StrId::STR_MEMORY_ERROR);
    else
      fail(StrId::STR_WEB_FETCH_FAILED);
    Storage.remove(RAW_PATH);
    return;
  }
  UiListActivity::loop();
}
void TextWebActivity::buildScreen(UiScreen& view) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  view.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                              static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  uiTextBand(view, I18N.get(status), view.theme().smallText, 2);
  if (screen == Screen::Page) {
    char timestamp[32]{};
    crossloop::personal::formatSyncTime(static_cast<uint32_t>(page.syncedAt), timestamp, sizeof(timestamp));
    if (timestamp[0]) uiTextBand(view, timestamp, view.theme().smallText);
  }
  view.spacer(static_cast<int16_t>(metrics.verticalSpacing));
  fui::ListProps props;
  props.items = rows;
  props.count = count;
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;
  syncListViewport(view, props);
  view.list(props);
}

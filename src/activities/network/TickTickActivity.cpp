#include "TickTickActivity.h"

#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <Memory.h>
#include <WiFi.h>

#include <algorithm>
#include <cstring>

#include "CrossPointSettings.h"
#include "WifiSelectionActivity.h"
#include "activities/util/ConfirmationActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "components/UiTextBand.h"
#include "util/CrossLoopPreferences.h"
#include "util/TickTickParser.h"

namespace fui = freeink::ui;
namespace data = crossloop::personal;
namespace prefs = crossloop::preferences;
namespace {
constexpr char RAW_PATH[] = "/.crosspoint/personal/ticktick.part";
constexpr char API[] = "https://api.ticktick.com/open/v1/project";
constexpr unsigned PAGE_ITEMS = 10;
}  // namespace
void TickTickActivity::onEnter() {
  UiListActivity::onEnter();
  if (!prefs::read(prefs::Key::TickTickProjects, projects)) status = StrId::STR_TICK_SETTINGS_FAILED;
  menu();
}
void TickTickActivity::onExit() {
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
const char* TickTickActivity::headerTitle() const { return tr(STR_PERSONAL_SLEEP_TICKTICK); }
void TickTickActivity::row(int index, const char* label) {
  snprintf(labels[index], sizeof(labels[index]), "%s", label);
  rows[index] = {};
  rows[index].label = labels[index];
  rows[index].actionValue = static_cast<int16_t>(index);
}
void TickTickActivity::setStatus(StrId value) {
  RenderLock lock(*this);
  status = completionConfirmed ? StrId::STR_TICK_COMPLETED_REFRESH_FAILED : value;
  completionConfirmed = false;
  requestUpdate();
}
void TickTickActivity::menu() {
  RenderLock lock(*this);
  cache.close();
  screen = Screen::Menu;
  closeRouting();
  timestamp[0] = 0;
  count = 6;
  nav.reset();
  row(0, tr(STR_TICK_SYNC));
  row(1, tr(STR_TICK_TASKS));
  row(2, tr(STR_TICK_LISTS));
  row(3, tr(STR_TICK_TOKEN));
  row(4, tr(STR_TICK_SET_SLEEP));
  row(5, tr(STR_TICK_DISCONNECT));
  requestUpdate();
}
bool TickTickActivity::selected(const char* id) const {
  const size_t length = strlen(id);
  size_t start = 0;
  while (start < projects.size()) {
    const auto comma = projects.find(',', start);
    const auto end = comma == std::string::npos ? projects.size() : comma;
    if (end - start == length && !projects.compare(start, length, id)) return true;
    start = end + 1;
  }
  return false;
}
void TickTickActivity::list(Screen requested, unsigned first) {
  RenderLock lock(*this);
  cache.close();
  screen = requested;
  closeRouting();
  nav.reset();
  count = 0;
  timestamp[0] = 0;
  if (!cache.open(requested == Screen::Projects ? data::Dataset::Projects : data::Dataset::Tasks)) {
    status = StrId::STR_TICK_NO_CACHE;
    requestUpdate();
    return;
  }
  data::formatSyncTime(cache.timestamp(), timestamp, sizeof(timestamp));
  status = requested == Screen::Projects ? StrId::STR_TICK_SELECT_LISTS : StrId::STR_TICK_CACHED;
  pageStart = first < cache.size() ? first : 0;
  visibleItems = std::min(PAGE_ITEMS, cache.size() - pageStart);
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
    row(count, item.title);
    if (requested == Screen::Projects) {
      snprintf(labels[count], sizeof(labels[count]), "%s %s", selected(item.id) ? "[x]" : "[ ]", item.title);
    }
    ++count;
  }
  if (!cache.size()) status = StrId::STR_TICK_EMPTY;
  requestUpdate();
}
void TickTickActivity::editToken() {
  auto keyboard =
      makeUniqueNoThrow<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_TICK_TOKEN), std::string{},
                                               prefs::maxLength(prefs::Key::TickTickToken), InputType::Password);
  if (!keyboard) {
    setStatus(StrId::STR_MEMORY_ERROR);
    return;
  }
  startActivityForResult(std::move(keyboard), [this](const ActivityResult& result) {
    if (result.isCancelled) return;
    const auto* entry = std::get_if<KeyboardResult>(&result.data);
    if (!entry || entry->text.empty() || !prefs::write(prefs::Key::TickTickToken, entry->text)) {
      setStatus(StrId::STR_TICK_SETTINGS_FAILED);
      return;
    }
    cache.close();
    const bool projectsRemoved = data::removeCache(data::Dataset::Projects);
    const bool tasksRemoved = data::removeCache(data::Dataset::Tasks);
    const bool selectionRemoved = prefs::erase(prefs::Key::TickTickProjects);
    projects.clear();
    if (!projectsRemoved || !tasksRemoved || !selectionRemoved) {
      menu();
      setStatus(StrId::STR_TICK_SETTINGS_FAILED);
      return;
    }
    connect(Job::Projects);
  });
}
void TickTickActivity::disconnect() {
  auto confirmation =
      makeUniqueNoThrow<ConfirmationActivity>(renderer, mappedInput, tr(STR_TICK_DISCONNECT), tr(STR_TICK_FORGET_HELP));
  if (!confirmation) {
    setStatus(StrId::STR_MEMORY_ERROR);
    return;
  }
  startActivityForResult(std::move(confirmation), [this](const ActivityResult& result) {
    if (result.isCancelled) return;
    cache.close();
    const bool tokenRemoved = prefs::erase(prefs::Key::TickTickToken);
    const bool selectionRemoved = prefs::erase(prefs::Key::TickTickProjects);
    const bool projectsRemoved = data::removeCache(data::Dataset::Projects);
    const bool tasksRemoved = data::removeCache(data::Dataset::Tasks);
    projects.clear();
    menu();
    setStatus(tokenRemoved && selectionRemoved && projectsRemoved && tasksRemoved ? StrId::STR_TICK_DISCONNECTED
                                                                                  : StrId::STR_TICK_SETTINGS_FAILED);
  });
}
void TickTickActivity::selectProject(unsigned index) {
  if (!cache.read(index, item)) {
    setStatus(StrId::STR_FILE_OPEN_FAILED);
    return;
  }
  std::string next;
  if (selected(item.id)) {
    size_t begin = 0;
    while (begin < projects.size()) {
      const auto comma = projects.find(',', begin);
      const auto end = comma == std::string::npos ? projects.size() : comma;
      const auto id = projects.substr(begin, end - begin);
      if (id != item.id) {
        if (!next.empty()) next += ',';
        next += id;
      }
      begin = end + 1;
    }
  } else {
    if (std::count(projects.begin(), projects.end(), ',') >= 7) {
      setStatus(StrId::STR_TICK_LIST_LIMIT);
      return;
    }
    next = projects;
    if (!next.empty()) next += ',';
    next += item.id;
  }
  if (!prefs::write(prefs::Key::TickTickProjects, next)) {
    setStatus(StrId::STR_TICK_SETTINGS_FAILED);
    return;
  }
  projects = std::move(next);
  list(Screen::Projects, pageStart);
}
void TickTickActivity::openTask(unsigned index) {
  RenderLock lock(*this);
  if (!cache.read(index, item)) {
    status = StrId::STR_FILE_OPEN_FAILED;
    requestUpdate();
    return;
  }
  screen = Screen::Detail;
  closeRouting();
  count = 2;
  nav.reset();
  row(0, tr(STR_TICK_COMPLETE));
  row(1, tr(STR_BACK));
  requestUpdate();
}
void TickTickActivity::completeTask() {
  auto confirmation = makeUniqueNoThrow<ConfirmationActivity>(renderer, mappedInput, tr(STR_TICK_COMPLETE), item.title);
  if (!confirmation) {
    setStatus(StrId::STR_MEMORY_ERROR);
    return;
  }
  startActivityForResult(std::move(confirmation), [this](const ActivityResult& result) {
    if (!result.isCancelled) connect(Job::Complete);
  });
}
void TickTickActivity::connect(Job requested) {
  completionConfirmed = false;
  job = requested;
  if (job == Job::Tasks && projects.empty()) {
    setStatus(StrId::STR_TICK_SELECT_LISTS);
    return;
  }
  if (WiFi.status() == WL_CONNECTED) {
    startJob();
    return;
  }
  ownsWifi = ownsWifi || WiFi.getMode() == WIFI_MODE_NULL;
  auto wifi = makeUniqueNoThrow<WifiSelectionActivity>(renderer, mappedInput);
  if (!wifi) {
    setStatus(StrId::STR_MEMORY_ERROR);
    return;
  }
  startActivityForResult(std::move(wifi), [this](const ActivityResult& result) {
    const auto* connected = std::get_if<WifiResult>(&result.data);
    if (!result.isCancelled && connected && connected->connected)
      startJob();
    else
      setStatus(StrId::STR_WIFI_CONN_FAILED);
  });
}
bool TickTickActivity::startRequest(const std::string& url, VerifiedDownload::Method method) {
  std::string token;
  if (!prefs::read(prefs::Key::TickTickToken, token) || token.empty()) {
    menu();
    setStatus(StrId::STR_TICK_SETUP_HELP);
    return false;
  }
  {
    RenderLock lock(*this);
    screen = Screen::Busy;
    closeRouting();
    count = 1;
    nav.reset();
    row(0, tr(STR_CANCEL));
    status = StrId::STR_TICK_SYNCING;
    timestamp[0] = 0;
  }
  requestUpdateAndWait();
  {
    RenderLock lock(*this);
    if (auto* fonts = renderer.getFontCacheManager()) fonts->releaseSdFontCaches();
  }
  if (!download.start(url, RAW_PATH, method == VerifiedDownload::Method::Get ? 512 * 1024 : 1024, std::move(token),
                      method)) {
    menu();
    setStatus(StrId::STR_MEMORY_ERROR);
    return false;
  }
  return true;
}
bool TickTickActivity::startNextProject() {
  if (projectOffset >= projects.size()) return false;
  const auto comma = projects.find(',', projectOffset);
  const auto end = comma == std::string::npos ? projects.size() : comma;
  const auto length = end - projectOffset;
  if (length >= sizeof(project)) return false;
  memcpy(project, projects.data() + projectOffset, length);
  project[length] = 0;
  projectOffset = end + 1;
  return data::validTickTickId(project) && startRequest(std::string(API) + "/" + project + "/data");
}
void TickTickActivity::startJob() {
  if (job == Job::Tasks && projects.empty()) {
    menu();
    setStatus(StrId::STR_TICK_SELECT_LISTS);
    return;
  }
  cache.close();
  if (!Storage.ensureDirectoryExists("/.crosspoint/personal")) {
    setStatus(StrId::STR_FILE_OPEN_FAILED);
    return;
  }
  if (job == Job::Complete) {
    if (!data::validTickTickId(item.id) || !data::validTickTickId(item.group)) {
      setStatus(StrId::STR_TICK_SYNC_FAILED);
      return;
    }
    startRequest(std::string(API) + "/" + item.group + "/task/" + item.id + "/complete",
                 VerifiedDownload::Method::PostEmpty);
    return;
  }
  if (!writer.begin(job == Job::Projects ? data::Dataset::Projects : data::Dataset::Tasks)) {
    setStatus(StrId::STR_FILE_OPEN_FAILED);
    return;
  }
  bool started;
  if (job == Job::Projects)
    started = startRequest(API);
  else {
    projectOffset = 0;
    started = startNextProject();
  }
  if (!started) {
    writer.cancel();
    if (screen == Screen::Busy) {
      menu();
      setStatus(StrId::STR_TICK_SYNC_FAILED);
    }
  }
}
void TickTickActivity::handleResult() {
  const auto result = download.result();
  const int http = download.httpStatus();
  download.stop();
  if (result != VerifiedDownload::Result::Ok) {
    writer.cancel();
    Storage.remove(RAW_PATH);
    menu();
    setStatus(http == 401 || http == 403                      ? StrId::STR_TICK_AUTH_FAILED
              : job == Job::Complete                          ? StrId::STR_TICK_COMPLETE_UNCERTAIN
              : result == VerifiedDownload::Result::Cancelled ? StrId::STR_TICK_CANCELLED
              : result == VerifiedDownload::Result::Memory    ? StrId::STR_MEMORY_ERROR
              : result == VerifiedDownload::Result::Clock     ? StrId::STR_WEB_CLOCK
                                                              : StrId::STR_TICK_SYNC_FAILED);
    return;
  }
  if (job == Job::Complete) {
    completionConfirmed = true;
    Storage.remove(RAW_PATH);
    // Refresh after the server confirms completion. A lost acknowledgement
    // leaves the old snapshot visible; no blind automatic mutation retry.
    job = Job::Tasks;
    startJob();
    return;
  }
  const bool parsed = data::parseTickTick(
      RAW_PATH, job == Job::Projects ? data::TickTickResponse::Projects : data::TickTickResponse::Tasks, project,
      writer);
  Storage.remove(RAW_PATH);
  if (!parsed) {
    writer.cancel();
    menu();
    setStatus(StrId::STR_TICK_RESPONSE_FAILED);
    return;
  }
  if (job == Job::Tasks && projectOffset < projects.size()) {
    if (!startNextProject()) {
      writer.cancel();
      menu();
      setStatus(StrId::STR_TICK_SYNC_FAILED);
    }
    return;
  }
  const bool saved = writer.commit();
  writer.cancel();
  if (!saved) {
    menu();
    setStatus(StrId::STR_TICK_SYNC_FAILED);
    return;
  }
  completionConfirmed = false;
  list(job == Job::Projects ? Screen::Projects : Screen::Tasks);
}
void TickTickActivity::activateIndex(int index) {
  app.clearTapFlash();
  if (screen == Screen::Busy) {
    download.cancel();
    return;
  }
  if (screen == Screen::Menu) {
    switch (index) {
      case 0:
        connect(Job::Tasks);
        return;
      case 1:
        list(Screen::Tasks);
        return;
      case 2:
        connect(Job::Projects);
        return;
      case 3:
        editToken();
        return;
      case 4: {
        const auto old = SETTINGS.sleepScreen;
        SETTINGS.sleepScreen = CrossPointSettings::PERSONAL_TICKTICK;
        const bool saved = SETTINGS.saveToFile();
        if (!saved) SETTINGS.sleepScreen = old;
        setStatus(saved ? StrId::STR_DONE : StrId::STR_TICK_SETTINGS_FAILED);
        return;
      }
      case 5:
        disconnect();
        return;
    }
  } else if (screen == Screen::Projects || screen == Screen::Tasks) {
    if (index == 0 && pageStart)
      list(screen, pageStart - PAGE_ITEMS);
    else if (index == 1 && pageStart + visibleItems < cache.size())
      list(screen, pageStart + PAGE_ITEMS);
    else if (index >= 2 && index < count) {
      if (screen == Screen::Projects)
        selectProject(pageStart + index - 2);
      else
        openTask(pageStart + index - 2);
    }
  } else if (screen == Screen::Detail) {
    if (index == 0)
      completeTask();
    else
      list(Screen::Tasks, pageStart);
  }
}
void TickTickActivity::onBackButton() {
  if (screen == Screen::Busy)
    download.cancel();
  else if (screen == Screen::Detail)
    list(Screen::Tasks, pageStart);
  else if (screen != Screen::Menu)
    menu();
  else
    finish();
}
void TickTickActivity::loop() {
  if (screen == Screen::Busy && download.done()) {
    handleResult();
    return;
  }
  UiListActivity::loop();
}
void TickTickActivity::buildScreen(UiScreen& view) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  view.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                              static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  uiTextBand(view, I18N.get(status), view.theme().smallText, status == StrId::STR_TICK_SETUP_HELP ? 3 : 2);
  if (timestamp[0]) uiTextBand(view, timestamp, view.theme().smallText);
  if (screen == Screen::Detail) {
    uiTextBand(view, item.title, view.theme().bodyText, 4);
    if (item.when[0]) uiTextBand(view, item.when, view.theme().smallText, 2);
    if (item.detail[0]) uiTextBand(view, item.detail, view.theme().smallText, 3);
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

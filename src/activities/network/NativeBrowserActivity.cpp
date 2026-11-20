#include "NativeBrowserActivity.h"
#if CROSSLOOP_NETSURF_PROBE
#include <HalMemory.h>
#include <I18n.h>
#include <Memory.h>
#include <WiFi.h>

#include <algorithm>
#include <initializer_list>

#include "BrowserMenuActivity.h"
#include "EngineMemory.h"
#include "FontBridge.h"
#include "HttpTransport.h"
#include "PlotBridge.h"
#include "WifiSelectionActivity.h"
#include "activities/library/SavedItemsActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "network/NativeBrowserTransport.h"
#include "network/WebUrl.h"
#include "util/SavedItems.h"

namespace fui = freeink::ui;
namespace {
constexpr fui::ActionId BACK = 1, RELOAD = 2, OPEN = 3, PREVIOUS = 4, NEXT = 5, PAGE = 6, MENU = 7;
constexpr int FOOTER_HEIGHT = 112;
constexpr size_t ENGINE_LIMIT = 2 * 1024 * 1024;
}  // namespace

void NativeBrowserActivity::logResources(const char* stage) const {
  const auto memory = engine_memory_snapshot();
  const auto internal = HalMemory::getInternalHeap();
  const auto psram = HalMemory::getPsramHeap();
  LOG_INF("NSWEB", "%s live=%u peak=%u denied=%u internal=%u/%u psram=%u/%u stack=%u", stage,
          static_cast<unsigned>(memory.live_bytes), static_cast<unsigned>(memory.peak_bytes),
          static_cast<unsigned>(memory.failed_allocations), static_cast<unsigned>(internal.freeBytes),
          static_cast<unsigned>(internal.largestBlockBytes), static_cast<unsigned>(psram.freeBytes),
          static_cast<unsigned>(psram.largestBlockBytes), static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
}

void NativeBrowserActivity::onEnter() {
  Activity::onEnter();
  resetUi();
  for (const auto id : {BACK, RELOAD, OPEN, PREVIOUS, NEXT, PAGE, MENU}) app.on(id, action, this);
  app.setScreen(controls, this);
  RenderLock lock(*this);
  nserror result = font_bridge_begin(&renderer, NOTOSANS_12_FONT_ID, NOTOSANS_18_FONT_ID, 12, 18);
  if (result == NSERROR_OK) result = plot_bridge_bind(&renderer);
  if (result == NSERROR_OK && probe_browser_snapshot().state == PROBE_BROWSER_COLD) {
    if (!engine_memory_begin(ENGINE_LIMIT)) result = NSERROR_NOMEM;
    if (result == NSERROR_OK)
      result = probe_browser_init(renderer.getScreenWidth(), renderer.getScreenHeight() - FOOTER_HEIGHT,
                                  &font_bridge_layout, &plot_bridge_table, millis());
    if (result == NSERROR_OK) result = native_fetch_register(&nativeBrowserTransport);
  }
  if (result == NSERROR_OK)
    result = probe_browser_resize(renderer.getScreenWidth(), renderer.getScreenHeight() - FOOTER_HEIGHT);
  failed = result != NSERROR_OK;
  lastError = result;
  loading = false;
  loadingSince = millis();
  lastState = probe_browser_snapshot().state;
  logResources("open");
  requestUpdate();
}

void NativeBrowserActivity::onExit() {
  // ActivityManager already owns RenderLock during exit and destruction.
  closeRouting();
  probe_browser_close();
  plot_bridge_reset();
  font_bridge_close();
  loading = false;
  if (ownsWifi) {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
  }
  logResources("close");
  Activity::onExit();
}

void NativeBrowserActivity::fail(nserror error) {
  RenderLock lock(*this);
  failed = true;
  loading = false;
  lastError = error;
  probe_browser_close();
  logResources("failed");
  requestUpdate();
}

void NativeBrowserActivity::enterUrl() {
  char address[crossloop::web::MAX_URL + 1]{};
  {
    RenderLock lock(*this);
    probe_browser_copy_url(address, sizeof(address));
  }
  const std::string current = crossloop::web::validUrl(address) ? address : "https://";
  auto keyboard = makeUniqueNoThrow<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_WEB_OPEN_URL), current,
                                                           crossloop::web::MAX_URL, InputType::Url);
  if (!keyboard) {
    fail(NSERROR_NOMEM);
    return;
  }
  startActivityForResult(std::move(keyboard), [this](const ActivityResult& result) {
    if (!result.isCancelled)
      if (const auto* value = std::get_if<KeyboardResult>(&result.data)) {
        navigate(value->text);
        return;
      }
    requestUpdate();
  });
}

void NativeBrowserActivity::showMenu() {
  char address[crossloop::web::MAX_URL + 1]{};
  bool ready, forward, editable;
  unsigned zoom;
  {
    RenderLock lock(*this);
    probe_browser_copy_url(address, sizeof(address));
    ready = !failed && probe_browser_snapshot().state == PROBE_BROWSER_READY;
    forward = probe_browser_can_forward();
    editable = probe_browser_can_type();
    zoom = probe_browser_zoom_percent();
  }
  const bool saved = crossloop::saved::contains(crossloop::saved::Kind::Web, address);
  auto menu =
      makeUniqueNoThrow<BrowserMenuActivity>(renderer, mappedInput, address, ready, forward, editable, saved, zoom);
  if (!menu) return;
  startActivityForResult(std::move(menu), [this](const ActivityResult& result) {
    if (!result.isCancelled)
      if (const auto* choice = std::get_if<MenuResult>(&result.data)) menuAction(choice->action);
  });
}
void NativeBrowserActivity::enterSearch(bool wikipedia) {
  auto keyboard = makeUniqueNoThrow<KeyboardEntryActivity>(
      renderer, mappedInput, wikipedia ? tr(STR_DREAM_SEARCH_WIKIPEDIA) : tr(STR_DREAM_SEARCH_GOOGLE), "", 256);
  if (!keyboard) return;
  startActivityForResult(std::move(keyboard), [this, wikipedia](const ActivityResult& result) {
    if (result.isCancelled) return;
    if (const auto* text = std::get_if<KeyboardResult>(&result.data)) {
      const auto url = crossloop::web::searchUrl(
          text->text, wikipedia ? crossloop::web::SearchEngine::Wikipedia : crossloop::web::SearchEngine::Google);
      if (!url.empty()) navigate(url);
    }
  });
}
void NativeBrowserActivity::enterField() {
  {
    RenderLock lock(*this);
    if (!probe_browser_can_type()) return;
  }
  auto keyboard = makeUniqueNoThrow<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_DREAM_FIELD_TEXT), "", 256);
  if (!keyboard) return;
  startActivityForResult(std::move(keyboard), [this](const ActivityResult& result) {
    if (result.isCancelled) return;
    if (const auto* text = std::get_if<KeyboardResult>(&result.data)) {
      RenderLock lock(*this);
      const auto status = probe_browser_replace_text(text->text.c_str());
      if (status != NSERROR_OK) GUI.drawPopup(renderer, tr(STR_DREAM_FIELD_FAILED));
      requestUpdate();
    }
  });
}
void NativeBrowserActivity::menuAction(int action) {
  switch (action) {
    case BrowserMenuActivity::Address:
      enterUrl();
      return;
    case BrowserMenuActivity::Google:
      enterSearch(false);
      return;
    case BrowserMenuActivity::Wikipedia:
      enterSearch(true);
      return;
    case BrowserMenuActivity::TypeField:
      enterField();
      return;
    case BrowserMenuActivity::Close:
      finish();
      return;
    case BrowserMenuActivity::Bookmarks: {
      auto bookmarks = makeUniqueNoThrow<SavedItemsActivity>(renderer, mappedInput, crossloop::saved::Kind::Web);
      if (bookmarks)
        startActivityForResult(std::move(bookmarks), [this](const ActivityResult& result) {
          if (!result.isCancelled)
            if (const auto* address = std::get_if<WebAddressResult>(&result.data)) navigate(address->url);
        });
      return;
    }
    case BrowserMenuActivity::SavePage: {
      char address[crossloop::web::MAX_URL + 1]{};
      struct probe_browser_snapshot snapshot;
      {
        RenderLock lock(*this);
        if (probe_browser_copy_url(address, sizeof(address)) != NSERROR_OK) return;
        snapshot = probe_browser_snapshot();
      }
      const auto saved =
          crossloop::saved::toggle(crossloop::saved::Kind::Web, snapshot.title[0] ? snapshot.title : address, address);
      RenderLock lock(*this);
      const auto message = saved == crossloop::saved::Result::Saved     ? StrId::STR_DREAM_BOOKMARK_SAVED
                           : saved == crossloop::saved::Result::Removed ? StrId::STR_DREAM_BOOKMARK_REMOVED
                           : saved == crossloop::saved::Result::Full    ? StrId::STR_DREAM_SAVED_FULL
                                                                        : StrId::STR_DREAM_SAVED_FAILED;
      GUI.drawPopup(renderer, I18N.get(message));
      requestUpdate();
      return;
    }
    default:
      break;
  }
  RenderLock lock(*this);
  nserror status = NSERROR_OK;
  const unsigned zoom = probe_browser_zoom_percent();
  switch (action) {
    case BrowserMenuActivity::Forward:
      status = probe_browser_forward();
      break;
    case BrowserMenuActivity::ZoomIn:
      status = probe_browser_zoom(std::min(200u, zoom + 25));
      break;
    case BrowserMenuActivity::ZoomOut:
      status = probe_browser_zoom(zoom > 100 ? zoom - 25 : 75);
      break;
    case BrowserMenuActivity::ZoomReset:
      status = probe_browser_zoom(100);
      break;
    case BrowserMenuActivity::SubmitField:
      status = probe_browser_submit_field();
      break;
    case BrowserMenuActivity::Reload:
      status = probe_browser_reload();
      break;
    default:
      return;
  }
  if (status != NSERROR_OK) {
    GUI.drawPopup(renderer, tr(STR_NATIVE_BROWSER_TEST_FAILED));
  }
  loading = probe_browser_snapshot().state == PROBE_BROWSER_LOADING;
  if (loading) closeRouting();
  loadingSince = millis();
  requestUpdate();
}

void NativeBrowserActivity::navigate(std::string url) {
  if (url.find("://") == std::string::npos) url.insert(0, "https://");
  if (!crossloop::web::validUrl(url)) {
    RenderLock lock(*this);
    GUI.drawPopup(renderer, tr(STR_WEB_BAD_URL));
    return;
  }
  requestedUrl = std::move(url);
  if (WiFi.status() == WL_CONNECTED) {
    loadUrl();
    return;
  }
  ownsWifi = ownsWifi || WiFi.getMode() == WIFI_MODE_NULL;
  auto wifi = makeUniqueNoThrow<WifiSelectionActivity>(renderer, mappedInput);
  if (!wifi) {
    fail(NSERROR_NOMEM);
    return;
  }
  startActivityForResult(std::move(wifi), [this](const ActivityResult& result) {
    const auto* connection = std::get_if<WifiResult>(&result.data);
    if (!result.isCancelled && connection && connection->connected)
      loadUrl();
    else
      requestUpdate();
  });
}

void NativeBrowserActivity::loadUrl() {
  RenderLock lock(*this);
  closeRouting();
  const nserror result = probe_browser_navigate(requestedUrl.c_str(), millis());
  failed = result != NSERROR_OK;
  lastError = result;
  loading = !failed;
  loadingSince = millis();
  if (failed) probe_browser_close();
  requestUpdate();
}

void NativeBrowserActivity::back() {
  bool leave = false;
  {
    RenderLock lock(*this);
    const bool cancelLoad = probe_browser_snapshot().state == PROBE_BROWSER_LOADING;
    if (cancelLoad || probe_browser_can_back()) {
      const nserror result = cancelLoad ? probe_browser_stop() : probe_browser_back();
      failed = result != NSERROR_OK;
      lastError = result;
      loading = !failed && probe_browser_snapshot().state == PROBE_BROWSER_LOADING;
      loadingSince = millis();
    } else
      leave = true;
    requestUpdate();
  }
  if (leave) finish();
}

void NativeBrowserActivity::action(const fui::ActionEvent& event, void* user) {
  auto& self = *static_cast<NativeBrowserActivity*>(user);
  self.app.clearTapFlash();
  if (event.action == MENU) {
    self.showMenu();
    return;
  }
  if (event.action == BACK) {
    self.back();
    return;
  }
  if (event.action == OPEN) {
    self.enterUrl();
    return;
  }
  RenderLock lock(self);
  if (event.action == RELOAD) {
    const nserror result = probe_browser_reload();
    self.failed = result != NSERROR_OK;
    self.lastError = result;
    self.loading = !self.failed && probe_browser_snapshot().state == PROBE_BROWSER_LOADING;
    self.loadingSince = millis();
  } else if (event.action == PREVIOUS || event.action == NEXT) {
    probe_browser_scroll((self.renderer.getScreenHeight() - FOOTER_HEIGHT - 24) * (event.action == PREVIOUS ? -1 : 1));
  }
  self.requestUpdate();
}

void NativeBrowserActivity::loop() {
  if (pendingInitialOpen) {
    pendingInitialOpen = false;
    if (!failed) navigate(requestedUrl);
    return;
  }
  const auto touch = routeTouch(mappedInput);
  if (touch.routed && app.invalidated()) requestUpdate();
  if (touch.event.action == PAGE) {
    RenderLock lock(*this);
    probe_browser_click(touch.snap.touchX, touch.snap.touchY);
    loading = probe_browser_snapshot().state == PROBE_BROWSER_LOADING;
    if (loading) closeRouting();
    loadingSince = millis();
    requestUpdate();
  } else if (touch)
    return;
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    back();
    return;
  }
  RenderLock lock(*this);
  if (failed) return;
  const auto oldSize = probe_browser_snapshot();
  if (oldSize.width != renderer.getScreenWidth() || oldSize.height != renderer.getScreenHeight() - FOOTER_HEIGHT)
    probe_browser_resize(renderer.getScreenWidth(), renderer.getScreenHeight() - FOOTER_HEIGHT);
  const auto swipe = mappedInput.wasSwipe();
  if (mappedInput.wasReleased(MappedInputManager::Button::NavNext) || swipe == MappedInputManager::SwipeDir::Up)
    probe_browser_scroll(renderer.getScreenHeight() - FOOTER_HEIGHT - 24);
  else if (mappedInput.wasReleased(MappedInputManager::Button::NavPrevious) ||
           swipe == MappedInputManager::SwipeDir::Down)
    probe_browser_scroll(-renderer.getScreenHeight() + FOOTER_HEIGHT + 24);
  const nserror result = probe_browser_pump(millis());
  const auto snapshot = probe_browser_snapshot();
  if (result != NSERROR_OK || snapshot.state == PROBE_BROWSER_FAILED || (loading && millis() - loadingSince > 60000)) {
    failed = true;
    loading = false;
    lastError = result != NSERROR_OK ? result : snapshot.error;
    probe_browser_close();
    logResources("load-failed");
    requestUpdate();
    return;
  }
  const bool wasLoading = loading;
  loading = snapshot.state == PROBE_BROWSER_LOADING;
  if (loading && !wasLoading) loadingSince = millis();
  if (snapshot.state != lastState) {
    lastState = snapshot.state;
    logResources("state");
  }
  if ((!loading && snapshot.dirty) || loading != wasLoading) requestUpdate();
}

void NativeBrowserActivity::controls(UiScreen& screen, void* user) {
  auto& self = *static_cast<NativeBrowserActivity*>(user);
  const int width = self.renderer.getScreenWidth();
  const int top = self.renderer.getScreenHeight() - FOOTER_HEIGHT;
  const auto state = probe_browser_snapshot().state;
  const bool pageReady = state == PROBE_BROWSER_READY;
  const bool pageLoading = state == PROBE_BROWSER_LOADING;
  if (!self.failed && pageReady)
    screen.frame().hit(fui::Rect{0, 0, static_cast<int16_t>(width), static_cast<int16_t>(top)}, PAGE, 0,
                       fui::InputTouch);
  const char* labels[] = {tr(STR_DREAM_ADDRESS), tr(STR_DREAM_MENU), tr(STR_WEB_RELOAD),
                          tr(STR_BACK),          tr(STR_PREV_PAGE),  tr(STR_NEXT_PAGE)};
  const fui::ActionId actions[] = {OPEN, MENU, RELOAD, BACK, PREVIOUS, NEXT};
  const int third = width / 3;
  for (int i = 0; i < 6; ++i) {
    fui::ButtonProps button;
    button.label = labels[i];
    button.action = actions[i];
    button.inputMask = fui::InputTouch;
    button.enabled = i == 3 || (i < 2 ? !pageLoading : !self.failed && pageReady);
    button.text = screen.theme().bodyText;
    const int x = (i % 3) * third + 8;
    const int y = top + (i < 3 ? 8 : 60);
    const int w = third - 16;
    fui::button(screen.frame(),
                fui::Rect{static_cast<int16_t>(x), static_cast<int16_t>(y), static_cast<int16_t>(w), 44}, button);
  }
}

void NativeBrowserActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto state = probe_browser_snapshot().state;
  if (failed) {
    const auto message =
        lastError == NSERROR_NOMEM
            ? StrId::STR_MEMORY_ERROR
            : (lastError == NSERROR_BAD_URL ? StrId::STR_WEB_BAD_URL : StrId::STR_NATIVE_BROWSER_TEST_FAILED);
    renderer.drawText(UI_12_FONT_ID, 20, 40, I18n::getInstance().get(message));
  } else if (state == PROBE_BROWSER_LOADING)
    renderer.drawText(UI_12_FONT_ID, 20, 40, tr(STR_LOADING));
  else if (state == PROBE_BROWSER_IDLE)
    renderer.drawText(UI_12_FONT_ID, 20, 40, tr(STR_WEB_OPEN_URL));
  else {
    plot_bridge_reset_clip();
    const nserror result = probe_browser_draw();
    plot_bridge_reset_clip();
    if (result != NSERROR_OK) {
      failed = true;
      lastError = result;
      renderer.clearScreen();
      renderer.drawText(UI_12_FONT_ID, 20, 40, tr(STR_NATIVE_BROWSER_TEST_FAILED));
    }
    logResources("draw");
  }
  renderUi();
  renderer.displayBuffer();
}
#endif

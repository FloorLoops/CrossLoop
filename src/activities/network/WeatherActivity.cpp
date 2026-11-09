#include "WeatherActivity.h"

#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <Memory.h>
#include <WiFi.h>

#include <cstdlib>

#include "CrossPointSettings.h"
#include "TextWebActivity.h"
#include "WifiSelectionActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "components/UiTextBand.h"
#include "util/PersonalCache.h"
#include "util/WeatherDrawing.h"

namespace fui = freeink::ui;
namespace prefs = crossloop::preferences;
namespace data = crossloop::personal;
namespace {
constexpr char RAW_PATH[] = "/.crosspoint/personal/weather.part";
}
void WeatherActivity::onEnter() {
  UiListActivity::onEnter();
  menu();
}
void WeatherActivity::onExit() {
  closeRouting();
  download.stop();
  Storage.remove(RAW_PATH);
  if (ownsWifi) {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
  }
  Activity::onExit();
}
const char* WeatherActivity::headerTitle() const { return tr(STR_WEATHER); }
void WeatherActivity::row(unsigned index, const char* label) {
  snprintf(labels[index], sizeof(labels[index]), "%s", label);
  rows[index] = {};
  rows[index].label = labels[index];
  rows[index].actionValue = index;
}
void WeatherActivity::menu(StrId message) {
  std::string units;
  const bool read = prefs::read(prefs::Key::Fahrenheit, units);
  RenderLock lock(*this);
  closeRouting();
  screen = Screen::Menu;
  status = read ? message : StrId::STR_TICK_SETTINGS_FAILED;
  fahrenheit = units != "0";
  nav.reset();
  count = 8;
  timestamp[0] = 0;
  row(0, tr(STR_WEATHER_REFRESH));
  row(1, tr(STR_WEATHER_SAVED));
  row(2, tr(STR_WEATHER_LOCATION));
  row(3, tr(STR_WEATHER_LATITUDE));
  row(4, tr(STR_WEATHER_LONGITUDE));
  row(5, fahrenheit ? tr(STR_WEATHER_USE_C) : tr(STR_WEATHER_USE_F));
  row(6, tr(STR_PERSONAL_USE_FOR_SLEEP));
  row(7, tr(STR_WEATHER_LICENCE));
  requestUpdate();
}
void WeatherActivity::forecast() {
  // Leave the forecast before replacing data that the render task reads.
  menu();
  if (!data::loadWeather(weather)) {
    menu(StrId::STR_WEATHER_NO_CACHE);
    return;
  }
  char synced[32]{};
  data::formatSyncTime(weather.syncedAt, synced, sizeof(synced));
  RenderLock lock(*this);
  closeRouting();
  screen = Screen::Forecast;
  status = StrId::STR_WEATHER_CACHED;
  snprintf(timestamp, sizeof(timestamp), tr(STR_PERSONAL_SNAPSHOT_TIME), synced[0] ? synced : "?");
  nav.reset();
  count = 2;
  row(0, tr(STR_WEATHER_REFRESH));
  row(1, tr(STR_PERSONAL_USE_FOR_SLEEP));
  requestUpdate();
}
void WeatherActivity::edit(prefs::Key key, StrId label) {
  std::string value;
  if (!prefs::read(key, value)) {
    menu(StrId::STR_TICK_SETTINGS_FAILED);
    return;
  }
  auto keyboard =
      makeUniqueNoThrow<KeyboardEntryActivity>(renderer, mappedInput, I18N.get(label), value, prefs::maxLength(key));
  if (!keyboard) {
    menu(StrId::STR_MEMORY_ERROR);
    return;
  }
  startActivityForResult(std::move(keyboard), [this, key](const ActivityResult& result) {
    if (result.isCancelled) return;
    const auto* entered = std::get_if<KeyboardResult>(&result.data);
    if (!entered || !prefs::validValue(key, entered->text)) {
      menu(StrId::STR_WEATHER_INVALID_LOCATION);
      return;
    }
    menu(prefs::write(key, entered->text) ? StrId::STR_WEATHER_SETTINGS_SAVED : StrId::STR_TICK_SETTINGS_FAILED);
  });
}
bool WeatherActivity::configureRequest() {
  std::string latitude, longitude, units;
  if (!prefs::read(prefs::Key::Latitude, latitude) || !prefs::read(prefs::Key::Longitude, longitude) ||
      !prefs::read(prefs::Key::LocationName, place) || !prefs::read(prefs::Key::Fahrenheit, units) ||
      latitude.empty() || longitude.empty())
    return false;
  fahrenheit = units != "0";
  // Canonical numbers prevent optional '+' or spaces from becoming query syntax.
  char coordinates[96];
  snprintf(coordinates, sizeof(coordinates), "latitude=%.6f&longitude=%.6f", strtod(latitude.c_str(), nullptr),
           strtod(longitude.c_str(), nullptr));
  if (place.empty()) place = latitude + ", " + longitude;
  requestUrl = std::string("https://api.open-meteo.com/v1/forecast?") + coordinates +
               "&current=temperature_2m,apparent_temperature,weather_code,is_day&daily=weather_code,temperature_2m_max,"
               "temperature_2m_min"
               "&forecast_days=3&timezone=auto&temperature_unit=" +
               (fahrenheit ? "fahrenheit" : "celsius");
  return true;
}
void WeatherActivity::connect() {
  if (!configureRequest()) {
    menu(StrId::STR_WEATHER_INVALID_LOCATION);
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
void WeatherActivity::startDownload() {
  if (!Storage.ensureDirectoryExists("/.crosspoint/personal")) {
    menu(StrId::STR_FILE_OPEN_FAILED);
    return;
  }
  {
    RenderLock lock(*this);
    closeRouting();
    screen = Screen::Busy;
    status = StrId::STR_WEATHER_LOADING;
    timestamp[0] = 0;
    nav.reset();
    count = 1;
    row(0, tr(STR_CANCEL));
  }
  requestUpdateAndWait();
  {
    RenderLock lock(*this);
    if (auto* fonts = renderer.getFontCacheManager()) fonts->releaseSdFontCaches();
  }
  if (!download.start(requestUrl, RAW_PATH, 32768)) menu(StrId::STR_MEMORY_ERROR);
}
void WeatherActivity::done() {
  const bool downloaded = download.result() == VerifiedDownload::Result::Ok;
  download.stop();
  const bool saved =
      downloaded && data::parseWeather(RAW_PATH, fahrenheit, place.c_str(), weather) && data::saveWeather(weather);
  Storage.remove(RAW_PATH);
  if (saved)
    forecast();
  else
    menu(StrId::STR_WEATHER_FAILED);
}
void WeatherActivity::activateIndex(int index) {
  app.clearTapFlash();
  if (screen == Screen::Busy) {
    download.cancel();
    return;
  }
  if (index == 0) {
    connect();
    return;
  }
  if ((screen == Screen::Forecast && index == 1) || index == 6) {
    const auto old = SETTINGS.sleepScreen;
    SETTINGS.sleepScreen = CrossPointSettings::PERSONAL_TODAY;
    const bool saved = SETTINGS.saveToFile();
    if (!saved) SETTINGS.sleepScreen = old;
    menu(saved ? StrId::STR_DONE : StrId::STR_TICK_SETTINGS_FAILED);
    return;
  }
  switch (index) {
    case 1:
      forecast();
      break;
    case 2:
      edit(prefs::Key::LocationName, StrId::STR_WEATHER_LOCATION);
      break;
    case 3:
      edit(prefs::Key::Latitude, StrId::STR_WEATHER_LATITUDE);
      break;
    case 4:
      edit(prefs::Key::Longitude, StrId::STR_WEATHER_LONGITUDE);
      break;
    case 5:
      menu(prefs::write(prefs::Key::Fahrenheit, fahrenheit ? "0" : "1") ? StrId::STR_WEATHER_SETTINGS_SAVED
                                                                        : StrId::STR_TICK_SETTINGS_FAILED);
      break;
    case 7:
      if (auto browser = makeUniqueNoThrow<TextWebActivity>(renderer, mappedInput, "https://open-meteo.com/en/licence"))
        startActivityForResult(std::move(browser), nullptr);
      break;
  }
}
void WeatherActivity::onBackButton() {
  if (screen == Screen::Busy)
    download.cancel();
  else if (screen == Screen::Forecast)
    menu();
  else
    finish();
}
void WeatherActivity::loop() {
  if (screen == Screen::Busy && download.done()) {
    done();
    return;
  }
  UiListActivity::loop();
}
void WeatherActivity::buildScreen(UiScreen& view) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  view.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                              static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  uiTextBand(view, I18N.get(status), view.theme().smallText, screen == Screen::Forecast ? 1 : 3);
  if (timestamp[0]) uiTextBand(view, timestamp, view.theme().smallText);
  if (screen == Screen::Forecast) {
    const auto area = view.takeTop(view.contentRect().width >= 664 ? 210 : 380);
    data::drawWeatherPanel(renderer, weather, area.x + 12, area.y, area.width - 24);
  }
  fui::ListProps props;
  props.items = rows;
  props.count = count;
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;
  syncListViewport(view, props);
  view.list(props);
}

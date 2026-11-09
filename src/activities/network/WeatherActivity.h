#pragma once

#include <I18n.h>

#include "activities/UiListActivity.h"
#include "network/VerifiedDownload.h"
#include "util/CrossLoopPreferences.h"
#include "util/WeatherData.h"

class WeatherActivity final : public UiListActivity {
 public:
  WeatherActivity(GfxRenderer& renderer, MappedInputManager& input) : UiListActivity("Weather", renderer, input) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  bool preventAutoSleep() override { return download.running(); }

 protected:
  int listCount() const override { return count; }
  const char* headerTitle() const override;
  void buildScreen(UiScreen& view) override;
  void activateIndex(int index) override;
  void onBackButton() override;

 private:
  enum class Screen { Menu, Forecast, Busy };
  void menu(StrId message = StrId::STR_WEATHER_HELP);
  void forecast();
  void edit(crossloop::preferences::Key key, StrId label);
  bool configureRequest();
  void connect();
  void startDownload();
  void done();
  void row(unsigned index, const char* label);
  Screen screen = Screen::Menu;
  StrId status = StrId::STR_WEATHER_HELP;
  VerifiedDownload download;
  crossloop::personal::WeatherData weather;
  std::string requestUrl;
  std::string place;
  bool fahrenheit = true;
  bool ownsWifi = false;
  char timestamp[96]{};
  freeink::ui::ListItem rows[8]{};
  char labels[8][96]{};
  int count = 0;
};

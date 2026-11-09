#pragma once

#include <I18n.h>

#include "activities/UiListActivity.h"
#include "network/VerifiedDownload.h"
#include "util/CalendarAgenda.h"

class CalendarActivity final : public UiListActivity {
 public:
  CalendarActivity(GfxRenderer& renderer, MappedInputManager& input) : UiListActivity("Calendar", renderer, input) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  bool preventAutoSleep() override { return download.running(); }

 private:
  enum class Screen { Menu, Agenda, Detail, Downloading, Preparing };
  int listCount() const override { return count; }
  const char* headerTitle() const override;
  void buildScreen(UiScreen& view) override;
  void activateIndex(int index) override;
  void onBackButton() override;
  void row(unsigned index, const char* label);
  void menu(StrId message = StrId::STR_CALENDAR_HELP);
  void list(unsigned first = 0);
  void detail(unsigned index);
  void edit();
  void disconnect();
  bool clearCaches();
  void connect();
  void startDownload();
  void done();
  Screen screen = Screen::Menu;
  StrId status = StrId::STR_CALENDAR_HELP;
  VerifiedDownload download;
  crossloop::calendar::Agenda agenda;
  crossloop::personal::Item item;
  std::string url;
  bool ownsWifi = false;
  unsigned pageStart = 0, visibleItems = 0;
  int count = 0;
  char timestamp[96]{};
  freeink::ui::ListItem rows[16]{};
  char labels[16][193]{};
  char dates[10][96]{};
};

#pragma once

#include <I18n.h>

#include "activities/UiListActivity.h"
#include "network/VerifiedDownload.h"
#include "util/PersonalCache.h"

class TickTickActivity final : public UiListActivity {
 public:
  TickTickActivity(GfxRenderer& renderer, MappedInputManager& input) : UiListActivity("TickTick", renderer, input) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  bool preventAutoSleep() override { return download.running(); }

 private:
  enum class Screen : uint8_t { Menu, Tasks, Projects, Detail, Busy };
  enum class Job : uint8_t { Projects, Tasks, Complete };
  int listCount() const override { return count; }
  const char* headerTitle() const override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void onBackButton() override;
  void menu();
  void list(Screen requested, unsigned first = 0);
  void row(int index, const char* label);
  void setStatus(StrId value);
  void editToken();
  void disconnect();
  void selectProject(unsigned index);
  void openTask(unsigned index);
  void completeTask();
  void connect(Job requested);
  void startJob();
  bool startNextProject();
  bool startRequest(const std::string& url, VerifiedDownload::Method method = VerifiedDownload::Method::Get);
  void handleResult();
  bool selected(const char* id) const;

  Screen screen = Screen::Menu;
  Job job = Job::Projects;
  VerifiedDownload download;
  crossloop::personal::CacheReader cache;
  crossloop::personal::CacheWriter writer;
  crossloop::personal::Item item;
  bool ownsWifi = false;
  bool completionConfirmed = false;
  StrId status = StrId::STR_TICK_SETUP_HELP;
  char timestamp[32]{};
  char project[129]{};
  std::string projects;
  size_t projectOffset = 0;
  unsigned pageStart = 0;
  unsigned visibleItems = 0;
  int count = 0;
  freeink::ui::ListItem rows[12]{};
  char labels[12][200]{};
};

#pragma once

#include "activities/UiListActivity.h"

class ReadingStatsActivity final : public UiListActivity {
 public:
  ReadingStatsActivity(GfxRenderer& renderer, MappedInputManager& input)
      : UiListActivity("ReadingStats", renderer, input) {}
  void onEnter() override;
  void onExit() override {
    closeRouting();
    Activity::onExit();
  }

 protected:
  int listCount() const override { return count; }
  const char* headerTitle() const override;
  void buildScreen(UiScreen& view) override;
  void activateIndex(int index) override;

 private:
  void reload();
  freeink::ui::ListItem rows[14]{};
  char labels[14][96]{};
  int count = 0;
  bool failed = false;
  uint16_t dailyGoal = 20;
  int goalRow = -1;
};

#pragma once
#if CROSSLOOP_DREAM_OS
#include "activities/UiListActivity.h"

class SystemHealthActivity final : public UiListActivity {
 public:
  SystemHealthActivity(GfxRenderer& renderer, MappedInputManager& input)
      : UiListActivity("SystemHealth", renderer, input) {}
  void onEnter() override;
  void onExit() override {
    closeRouting();
    Activity::onExit();
  }

 private:
  int listCount() const override { return 8; }
  const char* headerTitle() const override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void sample();
  freeink::ui::ListItem rows[8]{};
  char values[7][96]{};
};
#endif

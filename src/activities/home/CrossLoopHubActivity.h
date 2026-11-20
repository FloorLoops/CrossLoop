#pragma once
#if CROSSLOOP_DREAM_OS
#include "activities/UiTabListActivity.h"

class CrossLoopHubActivity final : public UiTabListActivity {
 public:
  CrossLoopHubActivity(GfxRenderer& renderer, MappedInputManager& input)
      : UiTabListActivity("CrossLoopHub", renderer, input) {}
  void onEnter() override;
  void onExit() override;

 private:
  int listCount() const override;
  const char* headerTitle() const override;
  int tabCount() const override { return 4; }
  int activeTab() const override { return tab; }
  const char* tabLabel(int index) const override;
  void onTabAction(int index) override;
  void stepTab(int direction) override;
  bool handleButtons() override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void refreshOverview();
  void openBrowser(const std::string& url = {});
  int tab = 0;
  char overview[112]{};
  freeink::ui::ListItem rows[6]{};
};
#endif

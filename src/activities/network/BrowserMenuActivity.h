#pragma once
#if CROSSLOOP_NETSURF_PROBE
#include "activities/UiListActivity.h"

class BrowserMenuActivity final : public UiListActivity {
 public:
  enum Action {
    Address,
    Google,
    Wikipedia,
    SavePage,
    Bookmarks,
    Forward,
    ZoomIn,
    ZoomOut,
    ZoomReset,
    TypeField,
    SubmitField,
    Reload,
    Close,
    Count
  };
  BrowserMenuActivity(GfxRenderer& renderer, MappedInputManager& input, std::string address, bool ready, bool forward,
                      bool editable, bool saved, unsigned zoom);
  void onExit() override {
    closeRouting();
    Activity::onExit();
  }

 private:
  int listCount() const override { return Count; }
  const char* headerTitle() const override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  std::string address;
  freeink::ui::ListItem rows[Count]{};
};
#endif

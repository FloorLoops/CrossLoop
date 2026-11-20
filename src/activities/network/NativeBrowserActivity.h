#pragma once
#if CROSSLOOP_NETSURF_PROBE
#include "ProbeBrowser.h"
#include "activities/Activity.h"
#include "components/UiAppHost.h"

class NativeBrowserActivity final : public Activity, private UiAppHost {
 public:
  NativeBrowserActivity(GfxRenderer& renderer, MappedInputManager& input, std::string initialUrl = {})
      : Activity("NativeBrowser", renderer, input),
        UiAppHost(renderer),
        requestedUrl(std::move(initialUrl)),
        pendingInitialOpen(!requestedUrl.empty()) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return loading; }

 private:
  static void controls(UiScreen& screen, void* user);
  static void action(const freeink::ui::ActionEvent& event, void* user);
  void enterUrl();
  void showMenu();
  void menuAction(int action);
  void enterSearch(bool wikipedia);
  void enterField();
  void navigate(std::string url);
  void loadUrl();
  void back();
  void fail(nserror error);
  void logResources(const char* stage) const;
  std::string requestedUrl;
  bool pendingInitialOpen = false;
  bool loading = false;
  bool failed = false;
  bool ownsWifi = false;
  nserror lastError = NSERROR_OK;
  uint32_t loadingSince = 0;
  probe_browser_state lastState = PROBE_BROWSER_COLD;
};
#endif

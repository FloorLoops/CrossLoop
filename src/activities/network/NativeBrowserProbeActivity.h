#pragma once
#if CROSSLOOP_NETSURF_PROBE
#include "ProbeBrowser.h"
#include "activities/Activity.h"
#include "components/UiAppHost.h"

// Isolated development screen. All browser calls share Activity's render lock.
class NativeBrowserProbeActivity final : public Activity, private UiAppHost {
 public:
  NativeBrowserProbeActivity(GfxRenderer& renderer, MappedInputManager& input)
      : Activity("NativeBrowserProbe", renderer, input), UiAppHost(renderer) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return loading; }

 private:
  static void controls(UiScreen& screen, void* user);
  static void action(const freeink::ui::ActionEvent& event, void* user);
  void openPage();
  void logResources(const char* stage) const;
  bool loading = false;
  bool failed = false;
  uint32_t openedAt = 0;
  probe_browser_state lastState = PROBE_BROWSER_COLD;
};
#endif

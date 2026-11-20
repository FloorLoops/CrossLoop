#include "NativeBrowserProbeActivity.h"
#if CROSSLOOP_NETSURF_PROBE
#include <HalMemory.h>
#include <I18n.h>

#include "BitmapBridge.h"
#include "EngineMemory.h"
#include "FontBridge.h"
#include "PlotBridge.h"
#include "fontIds.h"

namespace fui = freeink::ui;
namespace {
constexpr fui::ActionId BACK = 1;
constexpr fui::ActionId RELOAD = 2;
constexpr int FOOTER_HEIGHT = 64;
constexpr size_t ENGINE_LIMIT = 2 * 1024 * 1024;
}  // namespace

void NativeBrowserProbeActivity::logResources(const char* stage) const {
  const auto memory = engine_memory_snapshot();
  const auto internal = HalMemory::getInternalHeap();
  const auto psram = HalMemory::getPsramHeap();
  LOG_INF("NSPROBE", "%s engine live=%u peak=%u blocks=%u denied=%u internal=%u/%u psram=%u/%u stack=%u", stage,
          static_cast<unsigned>(memory.live_bytes), static_cast<unsigned>(memory.peak_bytes),
          static_cast<unsigned>(memory.live_allocations), static_cast<unsigned>(memory.failed_allocations),
          static_cast<unsigned>(internal.freeBytes), static_cast<unsigned>(internal.largestBlockBytes),
          static_cast<unsigned>(psram.freeBytes), static_cast<unsigned>(psram.largestBlockBytes),
          static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
}

void NativeBrowserProbeActivity::openPage() {
  RenderLock lock(*this);
  failed = false;
  loading = true;
  closeRouting();
  probe_browser_close();
  nserror result = font_bridge_begin(&renderer, NOTOSANS_12_FONT_ID, NOTOSANS_18_FONT_ID, 12, 18);
  if (result == NSERROR_OK) result = plot_bridge_bind(&renderer);
  const auto snapshot = probe_browser_snapshot();
  if (result == NSERROR_OK && snapshot.state == PROBE_BROWSER_COLD) {
    if (!engine_memory_begin(ENGINE_LIMIT)) result = NSERROR_NOMEM;
    if (result == NSERROR_OK)
      result = probe_browser_init(renderer.getScreenWidth(), renderer.getScreenHeight() - FOOTER_HEIGHT,
                                  &font_bridge_layout, &plot_bridge_table, millis());
  }
  if (result == NSERROR_OK)
    result = probe_browser_resize(renderer.getScreenWidth(), renderer.getScreenHeight() - FOOTER_HEIGHT);
  if (result == NSERROR_OK) result = probe_browser_open_fixture(millis());
  failed = result != NSERROR_OK;
  loading = !failed;
  openedAt = millis();
  lastState = probe_browser_snapshot().state;
  if (failed) LOG_ERR("NSPROBE", "Open failed: %d", static_cast<int>(result));
  logResources("open");
  requestUpdate();
}

void NativeBrowserProbeActivity::onEnter() {
  Activity::onEnter();
  resetUi();
  app.on(BACK, action, this);
  app.on(RELOAD, action, this);
  app.setScreen(controls, this);
  openPage();
}

void NativeBrowserProbeActivity::onExit() {
  // ActivityManager holds RenderLock throughout onExit and destruction.
  closeRouting();
  probe_browser_close();
  plot_bridge_reset();
  font_bridge_close();
  loading = false;
  logResources("close");
  Activity::onExit();
}

void NativeBrowserProbeActivity::action(const fui::ActionEvent& event, void* user) {
  auto& self = *static_cast<NativeBrowserProbeActivity*>(user);
  self.app.clearTapFlash();
  if (event.action == BACK)
    finish();
  else if (event.action == RELOAD)
    self.openPage();
}

void NativeBrowserProbeActivity::loop() {
  const auto touch = routeTouch(mappedInput);
  if (touch.routed && app.invalidated()) requestUpdate();
  if (touch) return;
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return;
  }
  RenderLock lock(*this);
  if (failed) return;
  const nserror result = probe_browser_pump(millis());
  const auto snapshot = probe_browser_snapshot();
  if (result != NSERROR_OK || snapshot.state == PROBE_BROWSER_FAILED || (loading && millis() - openedAt > 10000)) {
    failed = true;
    loading = false;
    probe_browser_close();
    LOG_ERR("NSPROBE", "Load failed: %d state=%d", static_cast<int>(result), static_cast<int>(snapshot.state));
    logResources("load-failed");
    requestUpdate();
    return;
  }
  loading = snapshot.state == PROBE_BROWSER_LOADING;
  if (snapshot.state != lastState) {
    lastState = snapshot.state;
    logResources("state");
    requestUpdate();
  }
}

void NativeBrowserProbeActivity::controls(UiScreen& screen, void* user) {
  auto& self = *static_cast<NativeBrowserProbeActivity*>(user);
  const int width = self.renderer.getScreenWidth();
  const int y = self.renderer.getScreenHeight() - FOOTER_HEIGHT + 8;
  const char* labels[] = {tr(STR_BACK), tr(STR_WEB_RELOAD)};
  const fui::ActionId actions[] = {BACK, RELOAD};
  for (int i = 0; i < 2; ++i) {
    fui::ButtonProps button;
    button.label = labels[i];
    button.action = actions[i];
    button.inputMask = fui::InputTouch;
    button.enabled = i == 0 || !self.loading;
    button.text = screen.theme().bodyText;
    fui::button(screen.frame(),
                fui::Rect{static_cast<int16_t>(12 + i * width / 2), static_cast<int16_t>(y),
                          static_cast<int16_t>(width / 2 - 24), 44},
                button);
  }
}

void NativeBrowserProbeActivity::render(RenderLock&&) {
  renderer.clearScreen();
  if (failed)
    renderer.drawText(UI_12_FONT_ID, 20, 40, tr(STR_NATIVE_BROWSER_TEST_FAILED));
  else if (loading)
    renderer.drawText(UI_12_FONT_ID, 20, 40, tr(STR_LOADING));
  else {
    plot_bridge_reset_clip();
    const nserror result = probe_browser_draw();
    plot_bridge_reset_clip();
    if (result != NSERROR_OK) {
      failed = true;
      renderer.clearScreen();
      renderer.drawText(UI_12_FONT_ID, 20, 40, tr(STR_NATIVE_BROWSER_TEST_FAILED));
      LOG_ERR("NSPROBE", "Draw failed: %d", static_cast<int>(result));
    }
    logResources("draw");
  }
  renderUi();
  renderer.displayBuffer();
}
#endif

#pragma once

#include <HalStorage.h>

#include <array>
#include <cstdint>

#include "activities/Activity.h"
#include "components/UiAppHost.h"

// Web pages have their own visible Links/Back/Next controls. Book navigation,
// profiles and remembered-book state are not changed by reading a saved page.
class WebReaderActivity final : public Activity, private UiAppHost {
 public:
  WebReaderActivity(GfxRenderer& renderer, MappedInputManager& input, std::string pageKey)
      : Activity("WebReader", renderer, input), UiAppHost(renderer), key(std::move(pageKey)) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  struct Line {
    uint16_t offset = 0;
    uint16_t length = 0;
  };
  static void controls(UiScreen& screen, void* user);
  static void action(const freeink::ui::ActionEvent& event, void* user);
  void turn(int direction);
  bool loadPage();
  int textWidth(size_t start, size_t end);
  static size_t nextCharacter(const char* text, size_t at, size_t length);

  std::string key;
  HalFile file;
  char text[8193]{};
  std::array<Line, 64> lines{};
  std::array<uint32_t, 1024> offsets{};
  uint16_t currentPage = 0;
  uint16_t lineCount = 0;
  uint32_t nextOffset = 0;
  uint32_t textBytes = 0;
  bool failed = false;
};

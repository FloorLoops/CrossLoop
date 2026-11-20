#pragma once
#if CROSSLOOP_DREAM_OS || CROSSLOOP_NETSURF_PROBE
#include <HalMemory.h>

#include "activities/UiListActivity.h"
#include "util/SavedItems.h"

class SavedItemsActivity final : public UiListActivity {
 public:
  SavedItemsActivity(GfxRenderer& renderer, MappedInputManager& input, crossloop::saved::Kind kind)
      : UiListActivity("SavedItems", renderer, input, true), kind(kind) {}
  void onEnter() override;
  void onExit() override;

 private:
  int listCount() const override { return count; }
  const char* headerTitle() const override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void onRowLongPress(int index) override;
  bool handleButtons() override;
  void reload();
  crossloop::saved::Table* table() const;
  crossloop::saved::Kind kind;
  HalMemory::PsramBuffer data;
  uint8_t ordinals[crossloop::saved::CAPACITY]{};
  freeink::ui::ListItem rows[crossloop::saved::CAPACITY]{};
  int count = 0;
  bool failed = false;
};
#endif

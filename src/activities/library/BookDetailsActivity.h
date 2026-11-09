#pragma once

#include <I18n.h>

#include "activities/UiListActivity.h"
#include "util/BookDetails.h"

class BookDetailsActivity final : public UiListActivity {
 public:
  BookDetailsActivity(GfxRenderer& renderer, MappedInputManager& input, std::string path)
      : UiListActivity("BookDetails", renderer, input), path(std::move(path)) {}
  void onEnter() override;
  void onExit() override;

 private:
  int listCount() const override { return 8; }
  const char* headerTitle() const override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void onBackButton() override;
  void rebuild(StrId message);
  char* field(unsigned index);
  const char* baseField(unsigned index) const;
  static size_t fieldSize(unsigned index);
  std::string path;
  crossloop::books::Details draft, embedded;
  bool writable = true;
  StrId status = StrId::STR_BOOK_DETAILS_HELP;
  freeink::ui::ListItem rows[8]{};
};

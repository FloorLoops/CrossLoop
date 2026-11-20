#pragma once

#include <I18n.h>

#include "activities/UiListActivity.h"
#include "network/VerifiedDownload.h"
#include "network/WebUrl.h"
#include "util/WebHistory.h"
#include "util/WebPageStore.h"

class TextWebActivity final : public UiListActivity {
 public:
  TextWebActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string initialUrl = {})
      : UiListActivity("TextWeb", renderer, mappedInput),
        feedsEnabled(initialUrl.empty()),
        requestedUrl(std::move(initialUrl)) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  bool preventAutoSleep() override { return download.running(); }

 private:
  enum class Screen : uint8_t { Home, Saved, Page, Downloading };
  int listCount() const override { return count; }
  const char* headerTitle() const override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void onBackButton() override;
  void showHome();
  void showSaved();
  bool showPage(const std::string& key);
  bool hasCachedPage(const std::string& key);
  void enterUrl();
  void search(crossloop::web::SearchEngine engine);
  void navigate(std::string url);
  void travel(bool forward);
  void restorePage();
  void fetchUrl(std::string url);
  void startDownload();
  void openText();
  void removePage();
  void setRow(int index, const char* label);
  void fail(StrId message);

  Screen screen = Screen::Home;
  VerifiedDownload download;
  bool ownsWifi = false;
  bool cacheWasFull = false;
  const bool feedsEnabled;
  StrId status = StrId::STR_WEB_DESCRIPTION;
  std::string requestedUrl;
  std::string currentKey;
  crossloop::web::History history;
  bool replacing = false;
  crossloop::web::PageInfo page;
  crossloop::web::PageLink link;
  freeink::ui::ListItem rows[42]{};
  char labels[42][128]{};
  char keys[32][33]{};
  int count = 0;
};

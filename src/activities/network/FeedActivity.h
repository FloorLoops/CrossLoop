#pragma once

#include <I18n.h>

#include <array>

#include "activities/UiListActivity.h"
#include "network/VerifiedDownload.h"
#include "util/PersonalCache.h"

class FeedActivity final : public UiListActivity {
 public:
  FeedActivity(GfxRenderer& renderer, MappedInputManager& input) : UiListActivity("Feeds", renderer, input) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  bool preventAutoSleep() override { return download.running(); }

 private:
  enum class Screen { Home, Articles, Busy };
  int listCount() const override { return count; }
  const char* headerTitle() const override;
  void buildScreen(UiScreen& view) override;
  void activateIndex(int index) override;
  void onBackButton() override;
  void row(int index, const char* label);
  void menu();
  void articles(unsigned first = 0);
  void editFeed(unsigned index);
  void connect();
  void beginSync();
  bool fetchNext();
  void done();
  void fail(StrId reason);
  bool loadFeeds();

  Screen screen = Screen::Home;
  VerifiedDownload download;
  crossloop::personal::CacheReader cache;
  crossloop::personal::CacheWriter writer;
  crossloop::personal::Item item;
  std::array<std::string, 4> feeds;
  unsigned nextFeed = 0;
  unsigned pageStart = 0;
  unsigned visibleItems = 0;
  bool ownsWifi = false;
  StrId status = StrId::STR_FEED_HELP;
  char timestamp[32]{};
  int count = 0;
  freeink::ui::ListItem rows[12]{};
  char labels[12][193]{};
};

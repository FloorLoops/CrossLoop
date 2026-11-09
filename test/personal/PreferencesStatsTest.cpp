#include <HalMemory.h>
#include <nvs.h>

#include <iostream>

#include "activities/reader/ReaderSession.h"
#include "util/CrossLoopPreferences.h"
#include "util/LocalReadingStats.h"

int checks = 0, failures = 0;
#define CHECK(x)                             \
  do {                                       \
    ++checks;                                \
    if (!(x)) {                              \
      ++failures;                            \
      std::cerr << __LINE__ << ": " #x "\n"; \
    }                                        \
  } while (0)
int main() {
  namespace prefs = crossloop::preferences;
  std::string value;
  CHECK(prefs::read(prefs::Key::TickTickToken, value) && value.empty());
  CHECK(prefs::write(prefs::Key::TickTickToken, "fake-token"));
  CHECK(prefs::read(prefs::Key::TickTickToken, value) && value == "fake-token");
  const auto firstCommitCount = fake_nvs::commits;
  fake_nvs::failWrite = true;
  CHECK(prefs::write(prefs::Key::TickTickToken, "fake-token"));
  CHECK(fake_nvs::commits == firstCommitCount);
  fake_nvs::failWrite = false;
  CHECK(!prefs::write(prefs::Key::TickTickToken, "bad\r\nheader"));
  CHECK(!prefs::write(prefs::Key::TickTickToken, std::string(2049, 'x')));
  CHECK(!prefs::write(prefs::Key::TickTickProjects, "../escape"));
  CHECK(prefs::write(prefs::Key::TickTickProjects, "a,b,c,d,e,f,g,h"));
  CHECK(!prefs::write(prefs::Key::TickTickProjects, "a,b,c,d,e,f,g,h,i"));
  CHECK(!prefs::write(prefs::Key::TickTickProjects, "a,,b"));
  CHECK(!prefs::write(prefs::Key::TickTickProjects, "a,"));
  CHECK(prefs::write(prefs::Key::CalendarUrl, "https://example.com/calendar.ics?key=fixture"));
  CHECK(!prefs::write(prefs::Key::CalendarUrl, "http://example.com/calendar.ics"));
  CHECK(!prefs::write(prefs::Key::CalendarUrl, "https://user:password@example.com/"));
  CHECK(prefs::write(prefs::Key::FeedUrls, "https://example.com/rss\nhttps://example.net/atom"));
  std::string longFeed = "https://example.com/rss?";
  longFeed.append(1024 - longFeed.size(), 'a');
  const std::string fourLongFeeds = longFeed + '\n' + longFeed + '\n' + longFeed + '\n' + longFeed;
  CHECK(fourLongFeeds.size() == 4099);
  CHECK(prefs::write(prefs::Key::FeedUrls, fourLongFeeds));
  CHECK(prefs::read(prefs::Key::FeedUrls, value) && value == fourLongFeeds);
  CHECK(!prefs::write(prefs::Key::FeedUrls, "https://example.com/rss\n"));
  CHECK(!prefs::write(prefs::Key::FeedUrls, "https://example.com/rss\n\nhttps://example.net/atom"));
  for (const std::string invalid : {"nan", "inf", "-inf", "91", "-91", "1junk"})
    CHECK(!prefs::write(prefs::Key::Latitude, invalid));
  CHECK(prefs::write(prefs::Key::Latitude, "40.0000"));
  CHECK(prefs::write(prefs::Key::Longitude, "-74.0000"));
  CHECK(!prefs::write(prefs::Key::Longitude, "181"));
  CHECK(prefs::write(prefs::Key::Fahrenheit, "1"));
  CHECK(!prefs::write(prefs::Key::Fahrenheit, "yes"));
  fake_nvs::failCommit = true;
  CHECK(!prefs::write(prefs::Key::TickTickToken, "replacement"));
  fake_nvs::failCommit = false;
  CHECK(prefs::read(prefs::Key::TickTickToken, value) && value == "fake-token");
  HalMemory::stats.freeBytes = 100;
  CHECK(!prefs::read(prefs::Key::TickTickToken, value) && value.empty());
  HalMemory::stats.freeBytes = 1024 * 1024;
  CHECK(prefs::erase(prefs::Key::TickTickToken));
  CHECK(prefs::read(prefs::Key::TickTickToken, value) && value.empty());
  const auto eraseCommitCount = fake_nvs::commits;
  CHECK(prefs::erase(prefs::Key::TickTickToken));
  CHECK(fake_nvs::commits == eraseCommitCount);
  CHECK(fake_nvs::handles.empty());

  crossloop::ReadingStatsData stats;
  CHECK(crossloop::loadReadingStats(stats) && stats.seconds == 0);
  CHECK(crossloop::recordReadingSession(125, 3, 0));
  CHECK(crossloop::loadReadingStats(stats) && stats.seconds == 125 && stats.turns == 3 && stats.sessions == 1 &&
        stats.days[0].date == 0);
  CHECK(crossloop::recordReadingSession(60, 1, 1794168000));
  CHECK(crossloop::loadReadingStats(stats) && stats.seconds == 185 && stats.turns == 4 && stats.days[0].seconds == 60);
  const auto date = stats.days[0].date;
  CHECK(date >= 20261108 && date <= 20261109);
  CHECK(stats.secondsOn(date) == 60);
  for (uint32_t i = 1; i <= 9; ++i) stats.add(i, 1, 20261000 + i);
  CHECK(stats.days[0].date == date);
  CHECK(stats.days[1].date == 20261009);
  CHECK(stats.days[6].date == 20261004);
  const auto before = fake_nvs::namespaces["crossloop_stats"]["reading"];
  fake_nvs::failWrite = true;
  CHECK(!crossloop::recordReadingSession(60, 1, 0));
  fake_nvs::failWrite = false;
  CHECK(fake_nvs::namespaces["crossloop_stats"]["reading"] == before);
  fake_nvs::namespaces["crossloop_stats"]["reading"][24] ^= 1;
  CHECK(!crossloop::loadReadingStats(stats));
  CHECK(!crossloop::recordReadingSession(60, 1, 0));
  CHECK(crossloop::resetReadingStats());
  CHECK(crossloop::loadReadingStats(stats) && stats.seconds == 0);
  CHECK(crossloop::readingDate(0) == 0 && crossloop::readingDate(4102444800LL) == 0);
  stats.seconds = UINT64_MAX - 1;
  stats.turns = UINT32_MAX;
  stats.sessions = UINT32_MAX;
  stats.add(200, 100, 0);
  CHECK(stats.seconds == UINT64_MAX && stats.turns == UINT32_MAX && stats.sessions == UINT32_MAX);

  ReaderSession session;
  CHECK(sizeof(session) <= 56);
  session.onRenderComplete(1000, 0, 0);
  session.noteTurn(true, true);
  session.onRenderComplete(11000, 0, 100);
  CHECK(session.localDurationSeconds() == 10 && session.durationSeconds() == 0 && session.forwardTurnCount() == 1);
  CHECK(!session.isEmitWorthy());
  session.noteTurn(false, true);
  session.onRenderComplete(21000, 0, 0);
  CHECK(session.localDurationSeconds() == 10 && session.forwardTurnCount() == 1);
  session.noteTurn(true, false);
  session.onRenderComplete(31000, 0, 100);
  CHECK(session.localDurationSeconds() == 10);
  session.noteTurn(true, true);
  session.onRenderComplete(31001, 100, 200);
  CHECK(session.localDurationSeconds() == 10);
  session.noteTurn(true, true);
  session.onRenderComplete(3601001, 1000, 300);
  CHECK(session.localDurationSeconds() == 1810 && session.durationSeconds() == 1800);
  CHECK(session.forwardTurnCount() == 2 && session.isEmitWorthy());
  session.reset();
  CHECK(session.localDurationSeconds() == 0 && session.durationSeconds() == 0 && session.forwardTurnCount() == 0);
  session.onRenderComplete(0xfffffc18u, 100, 0);
  session.noteTurn(true, true);
  session.onRenderComplete(4000, 105, 100);
  CHECK(session.localDurationSeconds() == 5 && session.durationSeconds() == 5);
  CHECK(fake_nvs::handles.empty());
  std::cout << checks << " preference/statistics checks; " << failures << " failures\n";
  return failures ? 1 : 0;
}

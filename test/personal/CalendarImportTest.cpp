#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "util/CalendarAgenda.h"
#include "util/CalendarDates.h"
#include "util/CalendarImport.h"

int checks = 0, failures = 0;
#define CHECK(x)                             \
  do {                                       \
    ++checks;                                \
    if (!(x)) {                              \
      ++failures;                            \
      std::cerr << __LINE__ << ": " #x "\n"; \
    }                                        \
  } while (0)
using namespace crossloop::calendar;
namespace data = crossloop::personal;
int64_t epoch(const char* value) {
  Date date;
  CHECK(parseDate(value, date));
  return civilSeconds(date);
}
std::string calendar(const std::string& body) {
  return "BEGIN:VCALENDAR\r\nVERSION:2.0\r\n" + body + "END:VCALENDAR\r\n";
}
std::string event(const std::string& body) { return "BEGIN:VEVENT\r\n" + body + "END:VEVENT\r\n"; }
ImportResult load(const std::string& text, const char* from = "20261108T000000Z",
                  const char* through = "20261115T235959Z") {
  fake::files["/raw"] = std::vector<uint8_t>(text.begin(), text.end());
  return importCalendar("/raw", epoch(from), epoch(through));
}
std::vector<data::Item> records() {
  data::CacheReader reader;
  CHECK(reader.open(data::Dataset::Calendar));
  std::vector<data::Item> result;
  for (unsigned i = 1; i < reader.size(); ++i) {
    data::Item item;
    CHECK(reader.read(i, item));
    result.push_back(item);
  }
  return result;
}
int main() {
#ifdef _WIN32
  _putenv_s("TZ", "UTC0");
  _tzset();
#else
  setenv("TZ", "UTC0", 1);
  tzset();
#endif
  const auto first = event(
      "UID:one\r\nDTSTART:20261108T090000Z\r\nDTEND:20261108T100000Z\r\nSUMMARY:Read\\, then "
      "rest\r\nLOCATION:Home\r\n");
  CHECK(load(calendar(first)) == ImportResult::Ok);
  auto items = records();
  CHECK(items.size() == 1 && std::string(items[0].title) == "Read, then rest");
  int64_t start = 0, end = 0;
  CHECK(eventSpan(items[0], start, end) && start == epoch("20261108T090000Z") && end - start == 3600);
  const auto prior = fake::files[data::cachePath(data::Dataset::Calendar)];
  const auto complete = calendar(first);
  for (size_t n = 0; n < complete.size(); ++n) {
    // A missing final CRLF is tolerated; the END component itself is required.
    if (n >= complete.size() - 2) continue;
    CHECK(load(complete.substr(0, n)) != ImportResult::Ok);
    CHECK(fake::files[data::cachePath(data::Dataset::Calendar)] == prior);
  }
  CHECK(load(calendar(
            event("UID:day\r\nDTSTART;VALUE=DATE:20261108\r\nDTEND;VALUE=DATE:20261110\r\nSUMMARY:Two days\r\n"))) ==
        ImportResult::Ok);
  items = records();
  CHECK(items.size() == 1 && (items[0].flags & data::AllDay));
  CHECK(eventSpan(items[0], start, end) && end - start == 2 * 86400);
  const auto repeating = event(
      "UID:repeat\r\nDTSTART:20261108T090000Z\r\nDURATION:PT1H\r\nRRULE:FREQ=DAILY;COUNT=3\r\nEXDATE:"
      "20261109T090000Z\r\nRDATE:20261112T090000Z,20261108T090000Z\r\nSUMMARY:Repeat\r\n");
  const auto moved = event(
      "UID:repeat\r\nRECURRENCE-ID:20261110T090000Z\r\nDTSTART:20261114T110000Z\r\nDURATION:PT1H\r\nSUMMARY:"
      "Moved\r\nSEQUENCE:2\r\n");
  CHECK(load(calendar(repeating + moved)) == ImportResult::Ok);
  items = records();
  CHECK(items.size() == 3);
  if (items.size() == 3) {
    CHECK(eventSpan(items[0], start, end) && start == epoch("20261108T090000Z"));
    CHECK(eventSpan(items[1], start, end) && start == epoch("20261112T090000Z"));
    CHECK(eventSpan(items[2], start, end) && start == epoch("20261114T110000Z") &&
          std::string(items[2].title) == "Moved");
  }
  const auto cancelled = event("UID:repeat\r\nRECURRENCE-ID:20261110T090000Z\r\nSTATUS:CANCELLED\r\nSEQUENCE:3\r\n");
  CHECK(load(calendar(repeating + moved + cancelled)) == ImportResult::Ok);
  CHECK(records().size() == 2);
  CHECK(load(calendar(cancelled + moved + repeating)) == ImportResult::Ok);
  CHECK(records().size() == 2);
  const std::string zone =
      "BEGIN:VTIMEZONE\r\nTZID:America/"
      "New_York\r\nBEGIN:DAYLIGHT\r\nDTSTART:19700308T020000\r\nTZOFFSETFROM:-0500\r\nTZOFFSETTO:-0400\r\nRRULE:FREQ="
      "YEARLY;BYMONTH=3;BYDAY=2SU\r\nEND:DAYLIGHT\r\nBEGIN:STANDARD\r\nDTSTART:19701101T020000\r\nTZOFFSETFROM:-"
      "0400\r\nTZOFFSETTO:-0500\r\nRRULE:FREQ=YEARLY;BYMONTH=11;BYDAY=1SU\r\nEND:STANDARD\r\nEND:VTIMEZONE\r\n";
  CHECK(
      load(
          calendar(
              event("UID:dst\r\nDTSTART;TZID=America/New_York:20261101T000000\r\nDURATION:P1D\r\nSUMMARY:DST day\r\n") +
              zone),
          "20261101T000000Z", "20261103T235959Z") == ImportResult::Ok);
  items = records();
  CHECK(items.size() == 1 && eventSpan(items[0], start, end) && start == epoch("20261101T040000Z") &&
        end - start == 25 * 3600);
  CHECK(load(calendar(zone + event("UID:fold\r\nDTSTART;TZID=America/"
                                   "New_York:20251102T014500\r\nRRULE:FREQ=YEARLY;BYMONTH=11;BYDAY=1SU;UNTIL="
                                   "20261101T063000Z\r\nSUMMARY:First repeated hour\r\n")),
             "20261101T000000Z", "20261102T000000Z") == ImportResult::Ok);
  items = records();
  CHECK(items.size() == 1 && eventSpan(items[0], start, end) && start == epoch("20261101T054500Z"));
  CHECK(load(calendar(first)) == ImportResult::Ok);
  const auto stable = fake::files[data::cachePath(data::Dataset::Calendar)];
  for (const auto& bad :
       {calendar(first + first), "BEGIN:VCALENDAR\r\n" + first + "END:VCALENDAR\r\n",
        calendar(event("UID:bad\r\nDTSTART:20260230T090000Z\r\n")),
        calendar(event("UID:bad\r\nDTSTART:20261108T090000Z\r\nDTEND:20261108T080000Z\r\n")),
        calendar(event("UID:bad\r\nDTSTART:20261108T090000Z\r\nDURATION:PT1H\r\nDTEND:20261108T100000Z\r\n")),
        calendar(event("UID:bad\r\nDTSTART:20261108T090000Z\r\nRRULE:FREQ=HOURLY\r\n")),
        calendar(event("UID:bad\r\nDTSTART;TZID=Missing/Zone:20261108T090000\r\n")),
        calendar(event("UID:bad\r\nDTSTART:20261108T090000Z\r\nDURATION:P1Y\r\n")),
        calendar(event("UID:bad\r\nDTSTART:20261108T090000Z\r\nDURATION:P367D\r\n")),
        calendar(event("UID:bad\r\nDTSTART:20261108T090000Z\r\nEXDATE;VALUE=DATE:20261108\r\n")),
        calendar(
            event("UID:bad\r\nDTSTART:20261108T090000Z\r\nRECURRENCE-ID;RANGE=THISANDFUTURE:20261108T090000Z\r\n")),
        calendar(first) + calendar(first)}) {
    CHECK(load(bad) != ImportResult::Ok);
    CHECK(fake::files[data::cachePath(data::Dataset::Calendar)] == stable);
  }
  fake::writesLeft = 32;
  CHECK(load(calendar(first)) == ImportResult::StorageError);
  fake::writesLeft = -1;
  CHECK(fake::files[data::cachePath(data::Dataset::Calendar)] == stable);
  fake::renameCalls = 0;
  fake::renameFailure = 2;
  CHECK(load(calendar(first)) == ImportResult::StorageError);
  fake::renameFailure = 0;
  items = records();
  CHECK(items.size() == 1 && std::string(items[0].id) == "one");
  CHECK(load(calendar("")) == ImportResult::Ok);
  CHECK(records().empty());
  const auto later = event("UID:later\r\nDTSTART:20261110T090000Z\r\nSUMMARY:Later\r\n");
  CHECK(load(calendar(later + first)) == ImportResult::Ok);
  Agenda agenda;
  data::Item item;
  CHECK(agenda.open() && agenda.size() == 2);
  CHECK(agenda.read(0, item) && std::string(item.id) == "one");
  CHECK(agenda.read(1, item) && std::string(item.id) == "later");
  agenda.close();
  CHECK(prepareCalendarPreview(epoch("20261108T120000Z")));
  data::CacheReader preview;
  CHECK(preview.open(data::Dataset::CalendarPreview) && preview.size() == 2);
  CHECK(preview.read(1, item) && std::string(item.id) == "later");
  preview.close();
  const auto previousPreview = fake::files[data::cachePath(data::Dataset::CalendarPreview)];
  CHECK(!prepareCalendarPreview(epoch("20261208T120000Z")));
  CHECK(fake::files[data::cachePath(data::Dataset::CalendarPreview)] == previousPreview);
  std::cout << checks << " calendar import checks; " << failures << " failures\n";
  return failures ? 1 : 0;
}
